/*****************************************************************************\
 *  qsw.c - Library routines for initiating jobs on QsNet. 
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if     HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <paths.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>	/* INT_MAX */
#include <pthread.h>
#include <stdio.h>
#include <elan3/elan3.h>
#include <elan3/elanvp.h>
#include <rms/rmscall.h>

#include <src/common/bitstring.h>
#include <src/common/log.h>
#include <src/common/pack.h>
#include <src/common/qsw.h>
#include <src/common/slurm_errno.h>

/*
 * Definitions local to this module.
 */
#define QSW_JOBINFO_MAGIC 	0xf00ff00e
#define QSW_LIBSTATE_MAGIC 	0xf00ff00f

/* we will allocate program descriptions in this range */
/* XXX note: do not start at zero as libelan shifts to get unique shm id */
#define QSW_PRG_START  		1
#define QSW_PRG_END    		INT_MAX
#define QSW_PRG_INVAL		(-1)

/* we allocate elan hardware context numbers in this range */
#define QSW_CTX_START		ELAN_USER_BASE_CONTEXT_NUM
#define QSW_CTX_END		ELAN_USER_TOP_CONTEXT_NUM
#define QSW_CTX_INVAL		(-1)

/* 
 * We are going to some trouble to keep these defs private so slurm
 * hackers not interested in the interconnect details can just pass around
 * the opaque types.  All use of the data structure internals is local to this
 * module.
 */
struct qsw_libstate {
	int ls_magic;
	int ls_prognum;
	int ls_hwcontext;
};

struct qsw_jobinfo {
	int             j_magic;
	int             j_prognum;
	ELAN_CAPABILITY j_cap;
	ELAN3_CTX      *j_ctx;
};

/* Copy library state */
#define _copy_libstate(dest, src) do { 			\
	assert((src)->ls_magic == QSW_LIBSTATE_MAGIC); 	\
	assert((dest)->ls_magic == QSW_LIBSTATE_MAGIC); 	\
	memcpy(dest, src, sizeof(struct qsw_libstate));	\
} while (0)

/* Lock on library state */
#define _lock_qsw() do {				\
	int err = pthread_mutex_lock(&qsw_lock);	\
	assert(err == 0);				\
} while (0)
#define _unlock_qsw() do {				\
	int err = pthread_mutex_unlock(&qsw_lock);	\
	assert(err == 0);				\
} while (0)

/*
 * Globals
 */
static qsw_libstate_t qsw_internal_state = NULL;
static pthread_mutex_t qsw_lock = PTHREAD_MUTEX_INITIALIZER;


/*
 * Allocate a qsw_libstate_t.
 *   lsp (IN)		store pointer to new instantiation here
 *   RETURN		0 on success, -1 on failure (sets errno)
 */
int
qsw_alloc_libstate(qsw_libstate_t *lsp)
{
	qsw_libstate_t new;

	assert(lsp != NULL);
	new = (qsw_libstate_t)malloc(sizeof(struct qsw_libstate));
	if (!new)
		slurm_seterrno_ret(ENOMEM);
	new->ls_magic = QSW_LIBSTATE_MAGIC;
	*lsp = new;
	return 0;
}

/*
 * Free a qsw_libstate_t.
 *   ls (IN)		qsw_libstate_t to free
 */
void
qsw_free_libstate(qsw_libstate_t ls)
{
	assert(ls->ls_magic == QSW_LIBSTATE_MAGIC);
	ls->ls_magic = 0;
	free(ls);
}

/*
 * Pack libstate structure in a format that can be shipped over the
 * network and unpacked on a different architecture.
 *   ls (IN)		libstate structure to be packed
 *   buffer (IN/OUT)	where to store packed data
 *   RETURN		#bytes unused in 'data'
 */
int
qsw_pack_libstate(qsw_libstate_t ls, Buf buffer)
{
	int offset;

	assert(ls->ls_magic == QSW_LIBSTATE_MAGIC);
	offset = get_buf_offset(buffer);

	pack32(ls->ls_magic, buffer);
	pack32(ls->ls_prognum, buffer);
	pack32(ls->ls_hwcontext, buffer);

	return (get_buf_offset(buffer) - offset);
}

/*
 * Unpack libstate packed by qsw_pack_libstate.
 *   ls (IN/OUT)	where to put libstate structure
 *   buffer (IN/OUT)	where to get packed data
 *   RETURN		#bytes unused or -1 on error (sets errno)
 */
int
qsw_unpack_libstate(qsw_libstate_t ls, Buf buffer)
{
	int offset;

	assert(ls->ls_magic == QSW_LIBSTATE_MAGIC);
	offset = get_buf_offset(buffer);

	unpack32(&ls->ls_magic, buffer);
	unpack32(&ls->ls_prognum, buffer);
	unpack32(&ls->ls_hwcontext, buffer);

	if (ls->ls_magic != QSW_LIBSTATE_MAGIC)
		slurm_seterrno_ret(EBADMAGIC_QSWLIBSTATE); /* corrupted libstate */

	return (get_buf_offset(buffer) - offset); 
}

/*
 * Seed the random number generator.  This can be called multiple times,
 * but srand48 will only be called once per program invocation.
 */
static void
_srand_if_needed(void)
{
	static int done = 0;

	if (!done) {
		srand48(getpid());
		done = 1;
	}
}

/*
 * Initialize this library, optionally restoring a previously saved state.
 *   oldstate (IN)	old state retrieved from qsw_fini() or NULL
 *   RETURN		0 on success, -1 on failure (sets errno)
 */
int
qsw_init(qsw_libstate_t oldstate)
{
	qsw_libstate_t new;

	assert(ELAN_MAX_VPS == QSW_MAX_TASKS);

	assert(qsw_internal_state == NULL);
	_srand_if_needed();
	if (qsw_alloc_libstate(&new) < 0)
		return -1; /* errno set by qsw_alloc_libstate */
	if (oldstate)
		_copy_libstate(new, oldstate);
	else {
		new->ls_prognum = QSW_PRG_START;
		new->ls_hwcontext = QSW_CTX_START;
	}
	qsw_internal_state = new;
	return 0;
}

/*
 * Finalize use of this library.  If 'savestate' is non-NULL, final
 * state is copied there before it is destroyed.
 *   savestate (OUT)	place to put state
 */
void
qsw_fini(qsw_libstate_t savestate)
{
	assert(qsw_internal_state != NULL);
	_lock_qsw();
	if (savestate)
		_copy_libstate(savestate, qsw_internal_state);
	qsw_free_libstate(qsw_internal_state);
	qsw_internal_state = NULL;
	_unlock_qsw();
}

/*
 * Allocate a qsw_jobinfo_t.
 *   jsp (IN)		store pointer to new instantiation here
 *   RETURN		0 on success, -1 on failure (sets errno)
 */
int
qsw_alloc_jobinfo(qsw_jobinfo_t *jp)
{
	qsw_jobinfo_t new; 

	assert(jp != NULL);
	new = (qsw_jobinfo_t)malloc(sizeof(struct qsw_jobinfo));
	if (!new)
		slurm_seterrno_ret(ENOMEM);
	new->j_magic = QSW_JOBINFO_MAGIC;
	new->j_ctx = NULL;
	*jp = new;
	return 0;
}

/*
 * Free a qsw_jobinfo_t.
 *   ls (IN)		qsw_jobinfo_t to free
 */
void
qsw_free_jobinfo(qsw_jobinfo_t j)
{
	assert(j->j_magic == QSW_JOBINFO_MAGIC);
	assert(j->j_ctx == NULL);
	j->j_magic = 0;
	free(j);
}

/*
 * Pack jobinfo structure in a format that can be shipped over the
 * network and unpacked on a different architecture.
 *   j (IN)		jobinfo structure to be packed
 *   buffer (OUT)		where to store packed data
 *   RETURN		#bytes unused in 'data' or -1 on error (sets errno)
 * NOTE: Keep in sync with QSW_PACK_SIZE above
 */
int
qsw_pack_jobinfo(qsw_jobinfo_t j, Buf buffer)
{
	int i, offset;

	assert(j->j_magic == QSW_JOBINFO_MAGIC);
	offset = get_buf_offset(buffer);

	pack32(j->j_magic, 		buffer);
	pack32(j->j_prognum, 		buffer);
	for (i = 0; i < 4; i++)
		pack32(j->j_cap.UserKey.Values[i], buffer);
	pack16(j->j_cap.Type, 		buffer);
	pack16(j->j_cap.padding, 	buffer);
	pack32(j->j_cap.Version,	buffer);
	pack32(j->j_cap.LowContext, 	buffer);
	pack32(j->j_cap.HighContext, 	buffer);
	pack32(j->j_cap.MyContext, 	buffer);
	pack32(j->j_cap.LowNode, 	buffer);
	pack32(j->j_cap.HighNode, 	buffer);
	pack32(j->j_cap.Entries, 	buffer);
	pack32(j->j_cap.RailMask, 	buffer);
	for (i = 0; i < ELAN_BITMAPSIZE; i++)
		pack32(j->j_cap.Bitmap[i], buffer);

	return (get_buf_offset(buffer) - offset);
}

/*
 * Unpack jobinfo structure packed by qsw_pack_jobinfo.
 *   j (IN/OUT)		where to store libstate structure
 *   buffer (OUT)		where to load packed data
 *   RETURN		#bytes unused in 'data' or -1 on error (sets errno)
 */
int
qsw_unpack_jobinfo(qsw_jobinfo_t j, Buf buffer)
{
	int i, offset;

	assert(j->j_magic == QSW_JOBINFO_MAGIC);
	offset = get_buf_offset(buffer);
 
	unpack32(&j->j_magic, 		buffer);
	unpack32(&j->j_prognum, 	buffer);
	for (i = 0; i < 4; i++)
		unpack32(&j->j_cap.UserKey.Values[i], buffer);
	unpack16(&j->j_cap.Type, 	buffer);
	unpack16(&j->j_cap.padding, 	buffer);	    
	unpack32(&j->j_cap.Version,	buffer); 	    
	unpack32(&j->j_cap.LowContext, 	buffer);
	unpack32(&j->j_cap.HighContext, buffer);
	unpack32(&j->j_cap.MyContext,	buffer);
	unpack32(&j->j_cap.LowNode, 	buffer);
	unpack32(&j->j_cap.HighNode,    buffer);
	unpack32(&j->j_cap.Entries, 	buffer);
	unpack32(&j->j_cap.RailMask, 	buffer);
	for (i = 0; i < ELAN_BITMAPSIZE; i++)
		unpack32(&j->j_cap.Bitmap[i], buffer);
	
	if (j->j_magic != QSW_JOBINFO_MAGIC)
		slurm_seterrno_ret(EBADMAGIC_QSWJOBINFO);

	return (get_buf_offset(buffer) - offset);
}

/*
 * Allocate a program description number.  Program descriptions, which are the
 * key abstraction maintained by the rms.o kernel module, must not be used
 * more than once simultaneously on a single node.  We allocate one to each
 * parallel job which more than meets this requirement.  A program description
 * can be compared to a process group, except there is no way for a process to
 * disassociate itself or its children from the program description.  
 * If the library is initialized, we allocate these consecutively, otherwise 
 * we generate a random one, assuming we are being called by a transient 
 * program like pdsh.  Ref: rms_prgcreate(3).
 */
static int
_generate_prognum(void)
{
	int new;

	if (qsw_internal_state) {
		_lock_qsw();
		new = qsw_internal_state->ls_prognum;
		if (new == QSW_PRG_END)
			qsw_internal_state->ls_prognum = QSW_PRG_START;
		else
			qsw_internal_state->ls_prognum++;
		_unlock_qsw();
	} else {
		_srand_if_needed();
		new = lrand48() % (QSW_PRG_END - QSW_PRG_START + 1);
		new += QSW_PRG_START;
	}
	return new;
}

/*
 * Elan hardware context numbers are an adapter resource that must not be used
 * more than once on a single node.  One is allocated to each process on the
 * node that will be communication over Elan.  In order for processes on the 
 * same node to communicate with one another and with other nodes across QsNet,
 * they must use contexts in the hi-lo range of a common capability.
 * If the library is initialized, we allocate these consecutively, otherwise 
 * we generate a random one, assuming we are being called by a transient 
 * program like pdsh.  Ref: rms_setcap(3).
 */
static int
_generate_hwcontext(int num)
{
	int new;

	if (qsw_internal_state) {
		_lock_qsw();
		if (qsw_internal_state->ls_hwcontext + num - 1 > QSW_CTX_END)
			qsw_internal_state->ls_hwcontext = QSW_CTX_START;
		new = qsw_internal_state->ls_hwcontext;
		qsw_internal_state->ls_hwcontext += num;
		_unlock_qsw();
	} else {
		_srand_if_needed();
		new = lrand48() % (QSW_CTX_END - (QSW_CTX_START + num - 1) - 1);
		new +=  QSW_CTX_START;
	}
	return new;
}

/*
 * Initialize the elan capability for this job.
 */
static void
_init_elan_capability(ELAN_CAPABILITY *cap, int nprocs, int nnodes,
		bitstr_t *nodeset, int cyclic_alloc)
{
	int i, node_num, full_node_cnt, min_procs_per_node, max_procs_per_node;

	/* Task count may not be identical for all nodes */
	full_node_cnt = nprocs % nnodes;
	min_procs_per_node = nprocs / nnodes;
	max_procs_per_node = (nprocs + nnodes - 1) / nnodes;

	_srand_if_needed();

	/* start with a clean slate */
	elan3_nullcap(cap);

	/* initialize for single rail and either block or cyclic allocation */
	if (cyclic_alloc)
		cap->Type = ELAN_CAP_TYPE_CYCLIC;
	else
		cap->Type = ELAN_CAP_TYPE_BLOCK;
	cap->Type |= ELAN_CAP_TYPE_MULTI_RAIL;
	cap->RailMask = 1;

	/* UserKey is 128 bits of randomness which should be kept private */
        for (i = 0; i < 4; i++)
		cap->UserKey.Values[i] = lrand48();

	/* set up hardware context range */
	cap->LowContext = _generate_hwcontext(max_procs_per_node);
	cap->HighContext = cap->LowContext + max_procs_per_node - 1;
	/* Note: not necessary to initialize cap->MyContext */

	/* set the range of nodes to be used and number of processes */
	cap->LowNode = bit_ffs(nodeset);
	assert(cap->LowNode != -1);
	cap->HighNode = bit_fls(nodeset);
	assert(cap->HighNode != -1);
	cap->Entries = nprocs;

#if USE_OLD_LIBELAN
	/* set the hw broadcast bit if consecutive nodes */
	if (abs(cap->HighNode - cap->LowNode) == nnodes - 1)
		cap->Type |= ELAN_CAP_TYPE_BROADCASTABLE;
#else
	/* set unconditionally per qsw gnat sw-elan/4334 */
	/* only time we don't want this is unsupported rev A hardware */
	cap->Type |= ELAN_CAP_TYPE_BROADCASTABLE;
#endif
	/*
	 * Set up cap->Bitmap, which describes the mapping of processes to 
	 * the nodes in the range of cap->LowNode - cap->Highnode.
	 * There are (nprocs * nnodes) significant bits in the mask, each 
 	 * representing a process slot.  Bits are off for process slots 
	 * corresponding to unallocated nodes.  For example, if nodes 4 and 6 
	 * are running two processes per node, bits 0,1 (corresponding to the 
	 * two processes on node 4) and bits 4,5 (corresponding to the two 
	 * processes running on node 6) are set.  
	 */
	node_num = 0;
	for (i = cap->LowNode; i <= cap->HighNode; i++) {
		if (bit_test(nodeset, i)) {
			int j, proc0, task_cnt;

			if (node_num++ < full_node_cnt)
				task_cnt = max_procs_per_node;
			else
				task_cnt = min_procs_per_node;
			for (j = 0; j < task_cnt; j++) {
				proc0 = (i - cap->LowNode) * max_procs_per_node;
				assert((proc0 + j) < (sizeof(cap->Bitmap) * 8));
				BT_SET(cap->Bitmap, (proc0 + j));
			}
		}
	}
}

/*
 * Create all the QsNet related information needed to set up a QsNet parallel
 * program and store it in the qsw_jobinfo struct.  
 * Call this on the "client" process, e.g. pdsh, srun, slurctld, etc..
 */
int
qsw_setup_jobinfo(qsw_jobinfo_t j, int nprocs, bitstr_t *nodeset, 
		int cyclic_alloc)
{
	int nnodes = bit_set_count(nodeset);

	assert(j != NULL);
	assert(j->j_magic == QSW_JOBINFO_MAGIC);

	/* sanity check on args */
	/* Note: ELAN_MAX_VPS is 512 on "old" Elan driver, 16384 on new. */
	if ((nprocs <= 0) || (nprocs > ELAN_MAX_VPS) || (nnodes <= 0)) {
		slurm_seterrno_ret(EINVAL);
	}
      
	/* initialize jobinfo */
	j->j_prognum = _generate_prognum();
	j->j_ctx = NULL;
	_init_elan_capability(&j->j_cap, nprocs, nnodes, nodeset, cyclic_alloc);

	return 0;
}

/*
 * Here are the necessary steps to set up to run an Elan MPI parallel program
 * (set of processes) on a node (possibly one of many allocated to the prog):
 *
 * Process 1	Process 2	|	Process 3
 * read args			|
 * fork	-------	rms_prgcreate	|
 * waitpid 	elan3_create	|
 * 		rms_prgaddcap	|
 *		fork N procs ---+------	rms_setcap
 *		wait all	|	setup RMS_ env	
 *				|	setuid, etc.
 *				|	exec mpi process
 *				|	
 *		exit		|
 * rms_prgdestroy		|
 * exit				|     (one pair of processes per mpi proc!)
 *
 * - The first fork is required because rms_prgdestroy can't occur in the 
 *   process that calls rms_prgcreate (since it is a member, ECHILD).
 * - The second fork is required when running multiple processes per node 
 *   because each process must announce its use of one of the hw contexts 
 *   in the range allocated in the capability.
 */

/*
 * Process 1: issue the rms_prgdestroy for the job.
 */
int
qsw_prgdestroy(qsw_jobinfo_t jobinfo)
{
	if (rms_prgdestroy(jobinfo->j_prognum) < 0) {
		/* translate errno values to more descriptive ones */
		switch (errno) {
			case ECHILD:
				slurm_seterrno(ECHILD_PRGDESTROY);
				break;
			case EEXIST:
				slurm_seterrno(EEXIST_PRGDESTROY);
				break;
			default:
				break;
		}
		return -1;
	}
	return 0;
}

/*
 * Process 2: Destroy the context after children are dead.
 */
void
qsw_prog_fini(qsw_jobinfo_t jobinfo)
{
	if (jobinfo->j_ctx) {
#if USE_OLD_LIBELAN
		_elan3_fini(jobinfo->j_ctx);
#else
		elan3_control_close(jobinfo->j_ctx);
#endif
		jobinfo->j_ctx = NULL;
	}
}

/*
 * Process 2: Create the context and make capability available to children.
 */
int
qsw_prog_init(qsw_jobinfo_t jobinfo, uid_t uid)
{
	int err;
#if USE_OLD_LIBELAN
	/* obtain an Elan context (not the same as a hardware context num!) */
	if ((jobinfo->j_ctx = _elan3_init(0)) == NULL) {
		slurm_seterrno(EELAN3INIT);
		goto fail;
	}
#else
	int i, nrails;
	nrails = elan3_nrails(&jobinfo->j_cap);

	for (i = 0; i < nrails; i++) {

		ELAN3_CTX *ctx;

		/* see qsw gnat sw-elan/4334: elan3_control_open can ret -1 */
		if ((ctx = elan3_control_open(i)) == NULL 
				|| ctx == (void *)-1) {
			slurm_seterrno(EELAN3CONTROL);
			goto fail;
		}
		
	
		/* make cap known via rms_getcap/rms_ncaps to members 
		 * of this prgnum */
		if (elan3_create(ctx, &jobinfo->j_cap) < 0) {
			/* XXX masking errno value better than not knowing 
			 * which function failed? */
		        error("elan3_create(%d): %m", i);
			slurm_seterrno(EELAN3CREATE); 
			goto fail;
		}
	}
#endif
	/* associate this process and its children with prgnum */
	if (rms_prgcreate(jobinfo->j_prognum, uid, 1) < 0) {
		/* translate errno values to more descriptive ones */
		switch (errno) {
			case EINVAL:
				slurm_seterrno(EINVAL_PRGCREATE);
				break;
			default:
				break;
		}
		goto fail;
	}

	if (rms_prgaddcap(jobinfo->j_prognum, 0, &jobinfo->j_cap) < 0) {
		/* translate errno values to more descriptive ones */
		switch (errno) {
			case ESRCH:
				slurm_seterrno(ESRCH_PRGADDCAP);
				break;
			case EFAULT:
				slurm_seterrno(EFAULT_PRGADDCAP);
				break;
			default:
				break;
		}
		goto fail;
	}

	/* note: _elan3_fini() destroys context and makes capability unavail */
	/* do it in qsw_prog_fini() after app terminates */
	return 0;
fail:
	err = errno; /* presrve errno in case _elan3_fini touches it */
	qsw_prog_fini(jobinfo); 
	slurm_seterrno(err);
	return -1;
}

/*
 * Process 3: Do the rms_setcap.
 */
int
qsw_setcap(qsw_jobinfo_t jobinfo, int procnum)
{
	/*
	 * Assign elan hardware context to current process.
	 * - arg1 (0 below) is an index into the kernel's list of caps for this 
	 *   program desc (added by rms_prgaddcap).  There will be
	 *   one per rail.
	 * - arg2 indexes the hw ctxt range in the capability
	 *   [cap->LowContext, cap->HighContext]
	 */
	if (rms_setcap(0, procnum) < 0) {
		/* translate errno values to more descriptive ones */
		switch (errno) {
			case EINVAL:
				slurm_seterrno(EINVAL_SETCAP);
				break;
			case EFAULT:
				slurm_seterrno(EFAULT_SETCAP);
				break;
			default:
				break;
		}
		return -1;
	}
	return 0;
}

/*
 * Return the local elan address (for rail 0) or -1 on failure.
 */
int
qsw_getnodeid(void)
{
	ELAN3_CTX *ctx = _elan3_init(0); /* rail 0 */
	int nodeid = -1;

	if (ctx) {
		nodeid = ctx->devinfo.Position.NodeId;
#if USE_OLD_LIBELAN
		_elan3_fini(ctx);
#else
		elan3_control_close(ctx);
#endif
	}
	if (nodeid == -1)
		slurm_seterrno(EGETNODEID);
	return nodeid;
}

/*
 * XXX - note qsw_getnodeid_byhost and qsw_gethost_bynodeid:
 * Eventually provide an autoconf option to look up mappings from a flat
 * file, or use the slurm.conf.  For now, assume that all QsNet systems
 * conform to RMS's hostname requirements.  They are:
 * 1) all hostnames with an elan adapter have a numerical suffix that 
 *    corresponds to the elanid.
 * 2) numerical suffixes never have leading zeros
 * 3) all hostnames without an elan adapter have a single character suffix.
 */

/*
 * Given a hostname, return the elanid or -1 on error.  
 * XXX - assumes RMS style hostnames (see above)
 */
int
qsw_getnodeid_byhost(char *host)
{
	char *p, *q, tmp[8];
	int id = -1;

	/* position p over last character to scan */
	if ((p = strchr(host, '.')))
		p--;
	else
		p = host + strlen(host) - 1;

	/* copy numerical suffix to tmp */
	tmp[sizeof(tmp) - 1] = '\0';
	q = &tmp[sizeof(tmp) - 2];
	while (q >= tmp && p >= host && isdigit(*p))
		*q-- = *p--;

	if (q < &tmp[sizeof(tmp) - 2])
		id = atoi(q + 1);

	if (id == -1)
		slurm_seterrno(EGETNODEID_BYHOST);
	return id;
}

/*
 * Given an elanid, determine the hostname.  Returns -1 on error or the number
 * of characters copied on success.  
 * XXX - assumes RMS style hostnames (see above)
 */
int
qsw_gethost_bynodeid(char *buf, int len, int id)
{
	char name[MAXHOSTNAMELEN];
	char *domainname;
	char *p;
	int res;

	if (id == -1)
		slurm_seterrno_ret(EGETHOST_BYNODEID);

	/* use the local hostname to determine 'base' name */
	if (gethostname(name, MAXHOSTNAMELEN) < 0)
		return -1; /* sets errno */
	if ((domainname = strchr(name, '.')))		
		*domainname++ = '\0';		/* save domainname for later */

	/* extract the 'base' name */
	if (qsw_getnodeid_byhost(name) == -1)	/* no numerical suffix */
		name[strlen(name) - 1] = '\0';	/*   assume one char suffix */
	else {					/* numerical suffix */
		p = name + strlen(name) - 1;
		while (p >= name && isdigit(*p))
			*p-- = '\0';
	}

	/* construct the new name from the id and the 'base' name. */
	if (domainname)
		res = snprintf(buf, len, "%s%d.%s", name, id, domainname);
	else
		res = snprintf(buf, len, "%s%d", name, id);

	return res;
}

/*
 * Send the specified signal to all members of a program description.
 * Returns -1 on failure and sets errno.  Ref: rms_prgsignal(3).
 */
int
qsw_prgsignal(qsw_jobinfo_t jobinfo, int signum)
{
	if (rms_prgsignal(jobinfo->j_prognum, signum) < 0) {
		/* translate errno values to more descriptive ones */
		switch (errno) {
			case EINVAL:
				slurm_seterrno(EINVAL_PRGSIGNAL);
				break;
			case ESRCH:
				slurm_seterrno(ESRCH_PRGSIGNAL);
				break;
			default:
				break;
		}
		return -1;
	}
	return 0;
}


#define TRUNC_BITMAP 1
static void
_print_capbitmap(FILE *fp, ELAN_CAPABILITY *cap)
{
	int bit_max = sizeof(cap->Bitmap)*8 - 1;
	int bit;
#if TRUNC_BITMAP
	bit_max = bit_max >= 64 ? 64 : bit_max;
#endif
	for (bit = bit_max; bit >= 0; bit--)
		fprintf(fp, "%c", BT_TEST(cap->Bitmap, bit) ? '1' : '0');
	fprintf(fp, "\n");
}

#define _USE_ELAN3_CAPABILITY_STRING 1
void
qsw_print_jobinfo(FILE *fp, struct qsw_jobinfo *jobinfo)
{
	ELAN_CAPABILITY *cap;
	char str[8192];

	assert(jobinfo->j_magic == QSW_JOBINFO_MAGIC);

	fprintf(fp, "__________________\n");
	fprintf(fp, "prognum=%d\n", jobinfo->j_prognum);

	cap = &jobinfo->j_cap;
	/* use elan3_capability_string as a shorter alternative for now */
#if _USE_ELAN3_CAPABILITY_STRING
	fprintf(fp, "%s\n", elan3_capability_string(cap, str));
#else 
	fprintf(fp, "cap.UserKey=%8.8x.%8.8x.%8.8x.%8.8x\n",
			cap->UserKey.Values[0], cap->UserKey.Values[1],
			cap->UserKey.Values[2], cap->UserKey.Values[3]);
	/*fprintf(fp, "cap.Version=%d\n", cap->Version);*/
	fprintf(fp, "cap.Type=0x%hx\n", cap->Type);
	fprintf(fp, "cap.padding=%hd\n", cap->padding);
	fprintf(fp, "cap.LowContext=%d\n", cap->LowContext);
	fprintf(fp, "cap.HighContext=%d\n", cap->HighContext);
	fprintf(fp, "cap.MyContext=%d\n", cap->MyContext);
	fprintf(fp, "cap.LowNode=%d\n", cap->LowNode);
	fprintf(fp, "cap.HighNode=%d\n", cap->HighNode);
	fprintf(fp, "cap.Entries=%d\n", cap->Entries);
	fprintf(fp, "cap.Railmask=0x%x\n", cap->RailMask);
	fprintf(fp, "cap.Bitmap=");
	_print_capbitmap(fp, cap);
#endif
	fprintf(fp, "\n------------------\n");
}
