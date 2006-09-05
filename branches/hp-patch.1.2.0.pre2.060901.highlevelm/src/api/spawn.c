/*****************************************************************************\
 *  spawn.c - spawn task functions for use by AIX/POE
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under 
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <slurm/slurm.h>

#include "src/common/slurm_step_layout.h"
#include "src/common/hostlist.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/api/step_ctx.h"

#define _DEBUG 0
#define _MAX_THREAD_COUNT 50

extern char **environ;

typedef enum {DSH_NEW, DSH_ACTIVE, DSH_DONE, DSH_FAILED} state_t;
typedef struct thd {
        pthread_t	thread;		/* thread ID */
	pthread_attr_t	attr;		/* pthread attributes */
        state_t		state;		/* thread state */
	time_t		tstart;		/* time thread started */
	slurm_msg_t *	req;		/* the message to send */
} thd_t;

static pthread_mutex_t thread_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t thread_cond   = PTHREAD_COND_INITIALIZER;
static uint32_t threads_active = 0;	/* currently active threads */

#if _DEBUG
static void	_dump_ctx(slurm_step_ctx ctx);
#endif
static int	_envcount(char **env);
static int	_p_launch(slurm_msg_t *req, slurm_step_ctx ctx);
static int	_sock_bind_wild(int sockfd);
static void *	_thread_per_node_rpc(void *args);
static int	_validate_ctx(slurm_step_ctx ctx);

/*
 * slurm_spawn - spawn tasks for the given job step context
 * IN ctx - job step context generated by slurm_step_ctx_create
 * IN fd_array  - array of socket file descriptors to connect with 
 *	stdin, stdout, and stderr of spawned task
 * RET SLURM_SUCCESS or SLURM_ERROR (with slurm_errno set)
 */
extern int slurm_spawn (slurm_step_ctx ctx, int *fd_array)
{
	spawn_task_request_msg_t *msg_array_ptr;
	int *sock_array;
	slurm_msg_t *req_array_ptr;
	int i, rc = SLURM_SUCCESS;
	uint16_t slurmd_debug = 0;
	char *env_var;
	/* hostlist_t hostlist = NULL; */
/* 	hostlist_iterator_t itr = NULL; */
	int task_cnt = 0;
	uint32_t *cpus = NULL;
	slurm_step_layout_t *step_layout = ctx->step_resp->step_layout;

	if ((ctx == NULL) ||
	    (ctx->magic != STEP_CTX_MAGIC) ||
	    (fd_array == NULL)) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}

	if (_validate_ctx(ctx))
		return SLURM_ERROR;

	/* get slurmd_debug level from SLURMD_DEBUG env var */
	env_var = getenv("SLURMD_DEBUG");
	if (env_var) {
		i = atoi(env_var);
		if (i >= 0)
			slurmd_debug = i;
	}

	/* validate fd_array and bind them to ports */
	sock_array = xmalloc(step_layout->node_cnt * sizeof(int));
	for (i=0; i<step_layout->node_cnt; i++) {
		if (fd_array[i] < 0) {
			slurm_seterrno(EINVAL);
			free(sock_array);
			return SLURM_ERROR;
		}
		sock_array[i] = _sock_bind_wild(fd_array[i]);
		if (sock_array[i] < 0) {
			slurm_seterrno(EINVAL);
			free(sock_array);
			return SLURM_ERROR;
		}
		listen(fd_array[i], 5);
		task_cnt += step_layout->tasks[i];
	}
	cpus = step_layout->tasks;

	msg_array_ptr = xmalloc(sizeof(spawn_task_request_msg_t) *
				step_layout->node_cnt);
	req_array_ptr = xmalloc(sizeof(slurm_msg_t) * 
				step_layout->node_cnt);

	//hostlist = hostlist_create(step_layout->node_list);
	//itr = hostlist_iterator_create(hostlist);

	for (i=0; i<step_layout->node_cnt; i++) {
		spawn_task_request_msg_t *r = &msg_array_ptr[i];
		slurm_msg_t              *m = &req_array_ptr[i];

		/* Common message contents */
		r->job_id	= ctx->job_id;
		r->uid		= ctx->user_id;
		r->argc		= ctx->argc;
		r->argv		= ctx->argv;
		r->cred		= ctx->step_resp->cred;
		r->job_step_id	= ctx->step_resp->job_step_id;
		r->envc		= ctx->envc;
		r->env		= ctx->env;
		r->cwd		= ctx->cwd;
		r->nnodes	= step_layout->node_cnt;
		r->nprocs	= task_cnt;
		r->switch_job	= ctx->step_resp->switch_job; 
		r->slurmd_debug	= slurmd_debug;
		/* Task specific message contents */
		r->global_task_id	= step_layout->tids[i][0];
		r->cpus_allocated	= cpus[i];
		r->srun_node_id	= (uint32_t) i;
		r->io_port	= ntohs(sock_array[i]);
		m->msg_type	= REQUEST_SPAWN_TASK;
		m->data		= r;
		
		memcpy(&m->address, &step_layout->node_addr[i], 
		       sizeof(slurm_addr));
#if		_DEBUG
		printf("tid=%d, fd=%d, port=%u, node_id=%u\n",
			step_layout->tids[i][0], 
		       fd_array[i], r->io_port, i);
#endif
	}
	//hostlist_iterator_destroy(itr);
	//hostlist_destroy(hostlist);
	rc = _p_launch(req_array_ptr, ctx);

	xfree(msg_array_ptr);
	xfree(req_array_ptr);
	xfree(sock_array);

	return rc;
}

/*
 * slurm_spawn_kill - send the specified signal to an existing job step
 * IN ctx - job step context generated by slurm_step_ctx_create
 * IN signal  - signal number
 * RET SLURM_SUCCESS or SLURM_ERROR (with slurm_errno set)
 */
extern int 
slurm_spawn_kill (slurm_step_ctx ctx, uint16_t signal)
{
	if ((ctx == NULL) ||
	    (ctx->magic != STEP_CTX_MAGIC)) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}

	return slurm_kill_job_step (ctx->job_id, 
			ctx->step_resp->job_step_id, signal);
}


static int _sock_bind_wild(int sockfd)
{
	socklen_t len;
	struct sockaddr_in sin;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(0);	/* bind ephemeral port */

	if (bind(sockfd, (struct sockaddr *) &sin, sizeof(sin)) < 0)
		return (-1);
	len = sizeof(sin);
	if (getsockname(sockfd, (struct sockaddr *) &sin, &len) < 0)
		return (-1);
	return (sin.sin_port);
}


/* validate the context of ctx, set default values as needed */
static int _validate_ctx(slurm_step_ctx ctx)
{
	int rc = SLURM_SUCCESS;

	if (ctx->cwd == NULL) {
		ctx->cwd = xmalloc(MAXPATHLEN);
		if (ctx->cwd == NULL) {
			slurm_seterrno(ENOMEM);
			return SLURM_ERROR;
		}
		getcwd(ctx->cwd, MAXPATHLEN);
	}

	if ((ctx->env_set == 0) && (ctx->envc == 0)) {
		ctx->envc	= _envcount(environ);
		ctx->env	= environ;
	}

#if _DEBUG
	_dump_ctx(ctx);
#endif
	return rc;
}


/* return number of elements in environment 'env' */
static int _envcount(char **env)
{
	int envc = 0;
	while (env[envc] != NULL)
		envc++;
	return (envc);
}


#if _DEBUG
/* dump the contents of a job step context */
static void	_dump_ctx(slurm_step_ctx ctx)
{
	int i, j;

	if ((ctx == NULL) ||
	    (ctx->magic != STEP_CTX_MAGIC)) {
		printf("Invalid _dump_ctx argument\n");
		return;
	}

	printf("job_id    = %u\n", ctx->job_id);
	printf("user_id   = %u\n", ctx->user_id);
	printf("num_hosts    = %u\n", ctx->num_hosts);
	printf("num_tasks = %u\n", ctx->num_tasks);
	printf("task_dist = %u\n", ctx->task_dist);

	printf("step_id   = %u\n", ctx->step_resp->job_step_id);
	printf("nodelist  = %s\n", ctx->step_resp->node_list);

	printf("cws       = %s\n", ctx->cwd);

	for (i=0; i<ctx->argc; i++) {
		printf("argv[%d]   = %s\n", i, ctx->argv[i]);
		if (i > 5) {
			printf("...\n");
			break;
		}
	}

	for (i=0; i<ctx->envc; i++) {
		if (strlen(ctx->env[i]) > 50)
			printf("env[%d]    = %.50s...\n", i, ctx->env[i]);
		else
			printf("env[%d]    = %s\n", i, ctx->env[i]);
		if (i > 5) {
			printf("...\n");
			break;
		}
	}

	for (i=0; i<ctx->step_resp->node_cnt; i++) {
		printf("host=%s cpus=%u tasks=%u",
			ctx->host[i], ctx->cpus[i], ctx->tasks[i]);
		for (j=0; j<ctx->tasks[i]; j++)
			printf(" tid[%d]=%u", j, ctx->tids[i][j]);
		printf("\n");
	}

	printf("\n");
}
#endif


/* parallel (multi-threaded) task launch, 
 * transmits all RPCs in parallel with timeout */
static int _p_launch(slurm_msg_t *req, slurm_step_ctx ctx)
{
	int rc = SLURM_SUCCESS, i;
	thd_t *thd;
	slurm_step_layout_t *step_layout = ctx->step_resp->step_layout;

	thd = xmalloc(sizeof(thd_t) * step_layout->node_cnt);
	if (thd == NULL) {
		slurm_seterrno(ENOMEM);
		return SLURM_ERROR;
	}

	for (i=0; i<step_layout->node_cnt; i++) {
		thd[i].state = DSH_NEW;
		thd[i].req = &req[i];
	}

	/* start all the other threads (up to _MAX_THREAD_COUNT active) */
	for (i=0; i<step_layout->node_cnt; i++) {
		/* wait until "room" for another thread */
		slurm_mutex_lock(&thread_mutex);
		while (threads_active >= _MAX_THREAD_COUNT) {
			pthread_cond_wait(&thread_cond, &thread_mutex);
		}

		slurm_attr_init(&thd[i].attr);
		(void) pthread_attr_setdetachstate(&thd[i].attr,
						PTHREAD_CREATE_DETACHED);
		while ((rc = pthread_create(&thd[i].thread, &thd[i].attr,
					    _thread_per_node_rpc,
	 				    (void *) &thd[i]))) {
			if (threads_active)
				pthread_cond_wait(&thread_cond, &thread_mutex);
			else {
				slurm_mutex_unlock(&thread_mutex);
				sleep(1);
				slurm_mutex_lock(&thread_mutex);
			}
		}
		slurm_attr_destroy(&thd[i].attr);

		threads_active++;
		slurm_mutex_unlock(&thread_mutex);
	}

	/* wait for all tasks to terminate */
	slurm_mutex_lock(&thread_mutex);
	for (i=0; i<step_layout->node_cnt; i++) {
		while (thd[i].state < DSH_DONE) {
			/* wait until another thread completes*/
			pthread_cond_wait(&thread_cond, &thread_mutex);
		}
	}
	slurm_mutex_unlock(&thread_mutex);

	xfree(thd);
	return rc;
}


/*
 * _thread_per_node_rpc - thread to issue an RPC to a single node
 * IN/OUT args - pointer to thd_t entry
 */
static void *_thread_per_node_rpc(void *args)
{
	int rc;
	thd_t *thread_ptr = (thd_t *) args;
	state_t new_state;

	thread_ptr->tstart = time(NULL);
	thread_ptr->state = DSH_ACTIVE;

	if (slurm_send_recv_rc_msg_only_one(thread_ptr->req, &rc, 0) < 0) {
		new_state = DSH_FAILED;
		goto cleanup;
	}

	switch (rc) {
		case SLURM_SUCCESS:
			new_state = DSH_DONE;
			break;
		default:
			slurm_seterrno(rc);
			new_state = DSH_FAILED;
	}

      cleanup:
	slurm_mutex_lock(&thread_mutex);
	thread_ptr->state = new_state;
	threads_active--;
	/* Signal completion so another thread can replace us */
	slurm_mutex_unlock(&thread_mutex);
	pthread_cond_signal(&thread_cond);

	return (void *) NULL;
}
