/*****************************************************************************\
 *  mysql_archive.c - functions dealing with the archiving.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mysql_archive.h"
#include "src/common/env.h"
#include "src/common/jobacct_common.h"

typedef struct {
	char *cluster_nodes;
	char *cpu_count;
	char *node_name;
	char *period_end;
	char *period_start;
	char *reason;
	char *reason_uid;
	char *state;
} local_event_t;

typedef struct {
	char *account;
	char *alloc_cpus;
	char *alloc_nodes;
	char *associd;
	char *blockid;
	char *comp_code;
	char *eligible;
	char *end;
	char *gid;
	char *id;
	char *jobid;
	char *kill_requid;
	char *name;
	char *nodelist;
	char *node_inx;
	char *partition;
	char *priority;
	char *qos;
	char *req_cpus;
	char *resvid;
	char *start;
	char *state;
	char *submit;
	char *suspended;
	char *track_steps;
	char *uid;
	char *wckey;
	char *wckey_id;
} local_job_t;

typedef struct {
	char *ave_cpu;
	char *ave_pages;
	char *ave_rss;
	char *ave_vsize;
	char *comp_code;
	char *cpus;
	char *id;
	char *kill_requid;
	char *max_pages;
	char *max_pages_node;
	char *max_pages_task;
	char *max_rss;
	char *max_rss_node;
	char *max_rss_task;
	char *max_vsize;
	char *max_vsize_node;
	char *max_vsize_task;
	char *min_cpu;
	char *min_cpu_node;
	char *min_cpu_task;
	char *name;
	char *nodelist;
	char *nodes;
	char *node_inx;
	char *period_end;
	char *period_start;
	char *period_suspended;
	char *state;
	char *stepid;
	char *sys_sec;
	char *sys_usec;
	char *tasks;
	char *task_dist;
	char *user_sec;
	char *user_usec;
} local_step_t;

typedef struct {
	char *associd;
	char *id;
	char *period_end;
	char *period_start;
} local_suspend_t;

/* if this changes you will need to edit the corresponding
 * enum below */
static char *event_req_inx[] = {
	"node_name",
	"cpu_count",
	"state",
	"period_start",
	"period_end",
	"reason",
	"reason_uid",
	"cluster_nodes",
};

enum {
	EVENT_REQ_NODE,
	EVENT_REQ_CPU,
	EVENT_REQ_STATE,
	EVENT_REQ_START,
	EVENT_REQ_END,
	EVENT_REQ_REASON,
	EVENT_REQ_REASON_UID,
	EVENT_REQ_CNODES,
	EVENT_REQ_COUNT
};

/* if this changes you will need to edit the corresponding
 * enum below */
static char *job_req_inx[] = {
	"account",
	"alloc_cpus",
	"alloc_nodes",
	"associd",
	"blockid",
	"comp_code",
	"eligible",
	"end",
	"gid",
	"id",
	"jobid",
	"kill_requid",
	"name",
	"nodelist",
	"node_inx",
	"partition",
	"priority",
	"qos",
	"req_cpus",
	"resvid",
	"start",
	"state",
	"submit",
	"suspended",
	"track_steps",
	"uid",
	"wckey",
	"wckeyid",
};

enum {
	JOB_REQ_ACCOUNT,
	JOB_REQ_ALLOC_CPUS,
	JOB_REQ_ALLOC_NODES,
	JOB_REQ_ASSOCID,
	JOB_REQ_BLOCKID,
	JOB_REQ_COMP_CODE,
	JOB_REQ_ELIGIBLE,
	JOB_REQ_END,
	JOB_REQ_GID,
	JOB_REQ_ID,
	JOB_REQ_JOBID,
	JOB_REQ_KILL_REQUID,
	JOB_REQ_NAME,
	JOB_REQ_NODELIST,
	JOB_REQ_NODE_INX,
	JOB_REQ_RESVID,
	JOB_REQ_PARTITION,
	JOB_REQ_PRIORITY,
	JOB_REQ_QOS,
	JOB_REQ_REQ_CPUS,
	JOB_REQ_START,
	JOB_REQ_STATE,
	JOB_REQ_SUBMIT,
	JOB_REQ_SUSPENDED,
	JOB_REQ_TRACKSTEPS,
	JOB_REQ_UID,
	JOB_REQ_WCKEY,
	JOB_REQ_WCKEYID,
	JOB_REQ_COUNT
};

/* if this changes you will need to edit the corresponding
 * enum below */
static char *step_req_inx[] = {
	"id",
	"stepid",
	"start",
	"end",
	"suspended",
	"name",
	"nodelist",
	"node_inx",
	"state",
	"kill_requid",
	"comp_code",
	"nodes",
	"cpus",
	"tasks",
	"task_dist",
	"user_sec",
	"user_usec",
	"sys_sec",
	"sys_usec",
	"max_vsize",
	"max_vsize_task",
	"max_vsize_node",
	"ave_vsize",
	"max_rss",
	"max_rss_task",
	"max_rss_node",
	"ave_rss",
	"max_pages",
	"max_pages_task",
	"max_pages_node",
	"ave_pages",
	"min_cpu",
	"min_cpu_task",
	"min_cpu_node",
	"ave_cpu"
};


enum {
	STEP_REQ_ID,
	STEP_REQ_STEPID,
	STEP_REQ_START,
	STEP_REQ_END,
	STEP_REQ_SUSPENDED,
	STEP_REQ_NAME,
	STEP_REQ_NODELIST,
	STEP_REQ_NODE_INX,
	STEP_REQ_STATE,
	STEP_REQ_KILL_REQUID,
	STEP_REQ_COMP_CODE,
	STEP_REQ_NODES,
	STEP_REQ_CPUS,
	STEP_REQ_TASKS,
	STEP_REQ_TASKDIST,
	STEP_REQ_USER_SEC,
	STEP_REQ_USER_USEC,
	STEP_REQ_SYS_SEC,
	STEP_REQ_SYS_USEC,
	STEP_REQ_MAX_VSIZE,
	STEP_REQ_MAX_VSIZE_TASK,
	STEP_REQ_MAX_VSIZE_NODE,
	STEP_REQ_AVE_VSIZE,
	STEP_REQ_MAX_RSS,
	STEP_REQ_MAX_RSS_TASK,
	STEP_REQ_MAX_RSS_NODE,
	STEP_REQ_AVE_RSS,
	STEP_REQ_MAX_PAGES,
	STEP_REQ_MAX_PAGES_TASK,
	STEP_REQ_MAX_PAGES_NODE,
	STEP_REQ_AVE_PAGES,
	STEP_REQ_MIN_CPU,
	STEP_REQ_MIN_CPU_TASK,
	STEP_REQ_MIN_CPU_NODE,
	STEP_REQ_AVE_CPU,
	STEP_REQ_COUNT
};

/* if this changes you will need to edit the corresponding
 * enum below */
static char *suspend_req_inx[] = {
	"id",
	"associd",
	"start",
	"end",
};

enum {
	SUSPEND_REQ_ID,
	SUSPEND_REQ_ASSOCID,
	SUSPEND_REQ_START,
	SUSPEND_REQ_END,
	SUSPEND_REQ_COUNT
};


static pthread_mutex_t local_file_lock = PTHREAD_MUTEX_INITIALIZER;
static int high_buffer_size = (1024 * 1024);

static void _pack_local_event(local_event_t *object,
			      uint16_t rpc_version, Buf buffer)
{
	packstr(object->cluster_nodes, buffer);
	packstr(object->cpu_count, buffer);
	packstr(object->node_name, buffer);
	packstr(object->period_end, buffer);
	packstr(object->period_start, buffer);
	packstr(object->reason, buffer);
	packstr(object->reason_uid, buffer);
	packstr(object->state, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_event(local_event_t *object,
			       uint16_t rpc_version, Buf buffer)
{
	uint32_t tmp32;

	unpackstr_ptr(&object->cluster_nodes, &tmp32, buffer);
	unpackstr_ptr(&object->cpu_count, &tmp32, buffer);
	unpackstr_ptr(&object->node_name, &tmp32, buffer);
	unpackstr_ptr(&object->period_end, &tmp32, buffer);
	unpackstr_ptr(&object->period_start, &tmp32, buffer);
	unpackstr_ptr(&object->reason, &tmp32, buffer);
	unpackstr_ptr(&object->reason_uid, &tmp32, buffer);
	unpackstr_ptr(&object->state, &tmp32, buffer);

	return SLURM_SUCCESS;
}

static void _pack_local_job(local_job_t *object,
			    uint16_t rpc_version, Buf buffer)
{
	packstr(object->account, buffer);
	packstr(object->alloc_cpus, buffer);
	packstr(object->alloc_nodes, buffer);
	packstr(object->associd, buffer);
	packstr(object->blockid, buffer);
	packstr(object->comp_code, buffer);
	packstr(object->eligible, buffer);
	packstr(object->end, buffer);
	packstr(object->gid, buffer);
	packstr(object->id, buffer);
	packstr(object->jobid, buffer);
	packstr(object->kill_requid, buffer);
	packstr(object->name, buffer);
	packstr(object->nodelist, buffer);
	packstr(object->node_inx, buffer);
	packstr(object->partition, buffer);
	packstr(object->priority, buffer);
	packstr(object->qos, buffer);
	packstr(object->req_cpus, buffer);
	packstr(object->resvid, buffer);
	packstr(object->start, buffer);
	packstr(object->state, buffer);
	packstr(object->submit, buffer);
	packstr(object->suspended, buffer);
	packstr(object->track_steps, buffer);
	packstr(object->uid, buffer);
	packstr(object->wckey, buffer);
	packstr(object->wckey_id, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_job(local_job_t *object,
			     uint16_t rpc_version, Buf buffer)
{
	uint32_t tmp32;

	unpackstr_ptr(&object->account, &tmp32, buffer);
	unpackstr_ptr(&object->alloc_cpus, &tmp32, buffer);
	unpackstr_ptr(&object->alloc_nodes, &tmp32, buffer);
	unpackstr_ptr(&object->associd, &tmp32, buffer);
	unpackstr_ptr(&object->blockid, &tmp32, buffer);
	unpackstr_ptr(&object->comp_code, &tmp32, buffer);
	unpackstr_ptr(&object->eligible, &tmp32, buffer);
	unpackstr_ptr(&object->end, &tmp32, buffer);
	unpackstr_ptr(&object->gid, &tmp32, buffer);
	unpackstr_ptr(&object->id, &tmp32, buffer);
	unpackstr_ptr(&object->jobid, &tmp32, buffer);
	unpackstr_ptr(&object->kill_requid, &tmp32, buffer);
	unpackstr_ptr(&object->name, &tmp32, buffer);
	unpackstr_ptr(&object->nodelist, &tmp32, buffer);
	unpackstr_ptr(&object->node_inx, &tmp32, buffer);
	unpackstr_ptr(&object->partition, &tmp32, buffer);
	unpackstr_ptr(&object->priority, &tmp32, buffer);
	unpackstr_ptr(&object->qos, &tmp32, buffer);
	unpackstr_ptr(&object->req_cpus, &tmp32, buffer);
	unpackstr_ptr(&object->resvid, &tmp32, buffer);
	unpackstr_ptr(&object->start, &tmp32, buffer);
	unpackstr_ptr(&object->state, &tmp32, buffer);
	unpackstr_ptr(&object->submit, &tmp32, buffer);
	unpackstr_ptr(&object->suspended, &tmp32, buffer);
	unpackstr_ptr(&object->track_steps, &tmp32, buffer);
	unpackstr_ptr(&object->uid, &tmp32, buffer);
	unpackstr_ptr(&object->wckey, &tmp32, buffer);
	unpackstr_ptr(&object->wckey_id, &tmp32, buffer);

	return SLURM_SUCCESS;
}

static void _pack_local_step(local_step_t *object,
			     uint16_t rpc_version, Buf buffer)
{
	packstr(object->ave_cpu, buffer);
	packstr(object->ave_pages, buffer);
	packstr(object->ave_rss, buffer);
	packstr(object->ave_vsize, buffer);
	packstr(object->comp_code, buffer);
	packstr(object->cpus, buffer);
	packstr(object->id, buffer);
	packstr(object->kill_requid, buffer);
	packstr(object->max_pages, buffer);
	packstr(object->max_pages_node, buffer);
	packstr(object->max_pages_task, buffer);
	packstr(object->max_rss, buffer);
	packstr(object->max_rss_node, buffer);
	packstr(object->max_rss_task, buffer);
	packstr(object->max_vsize, buffer);
	packstr(object->max_vsize_node, buffer);
	packstr(object->max_vsize_task, buffer);
	packstr(object->min_cpu, buffer);
	packstr(object->min_cpu_node, buffer);
	packstr(object->min_cpu_task, buffer);
	packstr(object->name, buffer);
	packstr(object->nodelist, buffer);
	packstr(object->nodes, buffer);
	packstr(object->node_inx, buffer);
	packstr(object->period_end, buffer);
	packstr(object->period_start, buffer);
	packstr(object->period_suspended, buffer);
	packstr(object->state, buffer);
	packstr(object->stepid, buffer);
	packstr(object->sys_sec, buffer);
	packstr(object->sys_usec, buffer);
	packstr(object->tasks, buffer);
	packstr(object->task_dist, buffer);
	packstr(object->user_sec, buffer);
	packstr(object->user_usec, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_step(local_step_t *object,
			      uint16_t rpc_version, Buf buffer)
{
	uint32_t tmp32;

	unpackstr_ptr(&object->ave_cpu, &tmp32, buffer);
	unpackstr_ptr(&object->ave_pages, &tmp32, buffer);
	unpackstr_ptr(&object->ave_rss, &tmp32, buffer);
	unpackstr_ptr(&object->ave_vsize, &tmp32, buffer);
	unpackstr_ptr(&object->comp_code, &tmp32, buffer);
	unpackstr_ptr(&object->cpus, &tmp32, buffer);
	unpackstr_ptr(&object->id, &tmp32, buffer);
	unpackstr_ptr(&object->kill_requid, &tmp32, buffer);
	unpackstr_ptr(&object->max_pages, &tmp32, buffer);
	unpackstr_ptr(&object->max_pages_node, &tmp32, buffer);
	unpackstr_ptr(&object->max_pages_task, &tmp32, buffer);
	unpackstr_ptr(&object->max_rss, &tmp32, buffer);
	unpackstr_ptr(&object->max_rss_node, &tmp32, buffer);
	unpackstr_ptr(&object->max_rss_task, &tmp32, buffer);
	unpackstr_ptr(&object->max_vsize, &tmp32, buffer);
	unpackstr_ptr(&object->max_vsize_node, &tmp32, buffer);
	unpackstr_ptr(&object->max_vsize_task, &tmp32, buffer);
	unpackstr_ptr(&object->min_cpu, &tmp32, buffer);
	unpackstr_ptr(&object->min_cpu_node, &tmp32, buffer);
	unpackstr_ptr(&object->min_cpu_task, &tmp32, buffer);
	unpackstr_ptr(&object->name, &tmp32, buffer);
	unpackstr_ptr(&object->nodelist, &tmp32, buffer);
	unpackstr_ptr(&object->nodes, &tmp32, buffer);
	unpackstr_ptr(&object->node_inx, &tmp32, buffer);
	unpackstr_ptr(&object->period_end, &tmp32, buffer);
	unpackstr_ptr(&object->period_start, &tmp32, buffer);
	unpackstr_ptr(&object->period_suspended, &tmp32, buffer);
	unpackstr_ptr(&object->state, &tmp32, buffer);
	unpackstr_ptr(&object->stepid, &tmp32, buffer);
	unpackstr_ptr(&object->sys_sec, &tmp32, buffer);
	unpackstr_ptr(&object->sys_usec, &tmp32, buffer);
	unpackstr_ptr(&object->tasks, &tmp32, buffer);
	unpackstr_ptr(&object->task_dist, &tmp32, buffer);
	unpackstr_ptr(&object->user_sec, &tmp32, buffer);
	unpackstr_ptr(&object->user_usec, &tmp32, buffer);

	return SLURM_SUCCESS;
}

static void _pack_local_suspend(local_suspend_t *object,
				uint16_t rpc_version, Buf buffer)
{
	packstr(object->associd, buffer);
	packstr(object->id, buffer);
	packstr(object->period_end, buffer);
	packstr(object->period_start, buffer);
}

/* this needs to be allocated before calling, and since we aren't
 * doing any copying it needs to be used before destroying buffer */
static int _unpack_local_suspend(local_suspend_t *object,
				 uint16_t rpc_version, Buf buffer)
{
	uint32_t tmp32;

	unpackstr_ptr(&object->associd, &tmp32, buffer);
	unpackstr_ptr(&object->id, &tmp32, buffer);
	unpackstr_ptr(&object->period_end, &tmp32, buffer);
	unpackstr_ptr(&object->period_start, &tmp32, buffer);

	return SLURM_SUCCESS;
}

static char *_make_archive_name(time_t period_start, time_t period_end,
				char *cluster_name,
				char *arch_dir, char *arch_type)
{
	struct tm time_tm;
	char start_char[32];
	char end_char[32];

	localtime_r((time_t *)&period_start, &time_tm);
	time_tm.tm_sec = 0;
	time_tm.tm_min = 0;
	time_tm.tm_hour = 0;
	time_tm.tm_mday = 1;
	snprintf(start_char, sizeof(start_char),
		 "%4.4u-%2.2u-%2.2u"
		 "T%2.2u:%2.2u:%2.2u",
		 (time_tm.tm_year + 1900),
		 (time_tm.tm_mon+1),
		 time_tm.tm_mday,
		 time_tm.tm_hour,
		 time_tm.tm_min,
		 time_tm.tm_sec);

	localtime_r((time_t *)&period_end, &time_tm);
	snprintf(end_char, sizeof(end_char),
		 "%4.4u-%2.2u-%2.2u"
		 "T%2.2u:%2.2u:%2.2u",
		 (time_tm.tm_year + 1900),
		 (time_tm.tm_mon+1),
		 time_tm.tm_mday,
		 time_tm.tm_hour,
		 time_tm.tm_min,
		 time_tm.tm_sec);

	/* write the buffer to file */
	return xstrdup_printf("%s/%s_%s_archive_%s_%s",
			      arch_dir, cluster_name, arch_type,
			      start_char, end_char);

}

static int _write_archive_file(Buf buffer, char *cluster_name,
			       time_t period_start, time_t period_end,
			       char *arch_dir, char *arch_type)
{
	int fd = 0;
	int error_code = SLURM_SUCCESS;
	char *old_file = NULL, *new_file = NULL, *reg_file = NULL;

	xassert(buffer);

	slurm_mutex_lock(&local_file_lock);

	/* write the buffer to file */
	reg_file = _make_archive_name(
		period_start, period_end, cluster_name, arch_dir, arch_type);

	debug("Storing %s archive for %s at %s",
	      arch_type, cluster_name, reg_file);
	old_file = xstrdup_printf("%s.old", reg_file);
	new_file = xstrdup_printf("%s.new", reg_file);

	fd = creat(new_file, 0600);
	if (fd < 0) {
		error("Can't save archive, create file %s error %m", new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount;
		char *data = (char *)get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
		while (nwrite > 0) {
			amount = write(fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}
		fsync(fd);
		close(fd);
	}

	if (error_code)
		(void) unlink(new_file);
	else {			/* file shuffle */
		int ign;	/* avoid warning */
		(void) unlink(old_file);
		ign =  link(reg_file, old_file);
		(void) unlink(reg_file);
		ign =  link(new_file, reg_file);
		(void) unlink(new_file);
	}
	xfree(old_file);
	xfree(reg_file);
	xfree(new_file);
	slurm_mutex_unlock(&local_file_lock);

	return error_code;
}

/* returns count of events archived or SLURM_ERROR on error */
static uint32_t _archive_events(mysql_conn_t *mysql_conn, char *cluster_name,
				time_t period_end, char *arch_dir)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	local_event_t event;
	Buf buffer;
	int error_code = 0, i = 0;

	/* if this changes you will need to edit the corresponding
	 * enum below */
	char *event_req_inx[] = {
		"node_name",
		"cpu_count",
		"state",
		"period_start",
		"period_end",
		"reason",
		"reason_uid",
		"cluster_nodes",
	};

	enum {
		EVENT_REQ_NODE,
		EVENT_REQ_CPU,
		EVENT_REQ_STATE,
		EVENT_REQ_START,
		EVENT_REQ_END,
		EVENT_REQ_REASON,
		EVENT_REQ_REASON_UID,
		EVENT_REQ_CNODES,
		EVENT_REQ_COUNT
	};

	xfree(tmp);
	xstrfmtcat(tmp, "%s", event_req_inx[0]);
	for(i=1; i<EVENT_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", event_req_inx[i]);
	}

	/* get all the events started before this time listed */
	query = xstrdup_printf("select %s from %s where period_start <= %d "
			       "&& period_end != 0 order by period_start asc",
			       tmp, event_table, period_end);
	xfree(tmp);

//	START_TIMER;
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if(!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = init_buf(high_buffer_size);
	pack16(SLURMDBD_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_EVENTS, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while((row = mysql_fetch_row(result))) {
		if(!period_start)
			period_start = atoi(row[EVENT_REQ_START]);

		memset(&event, 0, sizeof(local_event_t));

		event.cluster_nodes = row[EVENT_REQ_CNODES];
		event.cpu_count = row[EVENT_REQ_CPU];
		event.node_name = row[EVENT_REQ_NODE];
		event.period_end = row[EVENT_REQ_END];
		event.period_start = row[EVENT_REQ_START];
		event.reason = row[EVENT_REQ_REASON];
		event.reason_uid = row[EVENT_REQ_REASON_UID];
		event.state = row[EVENT_REQ_STATE];

		_pack_local_event(&event, SLURMDBD_VERSION, buffer);
	}
	mysql_free_result(result);

//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);

	error_code = _write_archive_file(buffer, cluster_name,
					 period_start, period_end,
					 arch_dir, "event");
	free_buf(buffer);

	if(error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_events(uint16_t rpc_version, Buf buffer,
			  char *cluster_name, uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL;
	local_event_t object;
	int i = 0;

	xstrfmtcat(insert, "insert into %s (%s", event_table, event_req_inx[0]);
	xstrcat(format, "('%s'");
	for(i=1; i<EVENT_REQ_COUNT; i++) {
		xstrfmtcat(insert, ", %s", event_req_inx[i]);
		xstrcat(format, ", '%s'");
	}
	xstrcat(insert, ") values ");
	xstrcat(format, ")");
	for(i=0; i<rec_cnt; i++) {
		memset(&object, 0, sizeof(local_event_t));
		if(_unpack_local_event(&object, rpc_version, buffer)
		   != SLURM_SUCCESS) {
			error("issue unpacking");
			xfree(format);
			xfree(insert);
			break;
		}
		if(i)
			xstrcat(insert, ", ");

		xstrfmtcat(insert, format,
			   object.cluster_nodes,
			   object.cpu_count,
			   object.node_name,
			   object.period_end,
			   object.period_start,
			   object.reason,
			   object.reason_uid,
			   object.state);

	}
//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);
	xfree(format);

	return insert;
}

/* returns count of jobs archived or SLURM_ERROR on error */
static uint32_t _archive_jobs(mysql_conn_t *mysql_conn, char *cluster_name,
			      time_t period_end, char *arch_dir)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	local_job_t job;
	Buf buffer;
	int error_code = 0, i = 0;

	xfree(tmp);
	xstrfmtcat(tmp, "%s", job_req_inx[0]);
	for(i=1; i<JOB_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", job_req_inx[i]);
	}

	/* get all the events started before this time listed */
	query = xstrdup_printf("select %s from %s where "
			       "submit < %d && end != 0 && !deleted "
			       "order by submit asc",
			       tmp, job_table, period_end);
	xfree(tmp);

//	START_TIMER;
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if(!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = init_buf(high_buffer_size);
	pack16(SLURMDBD_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_GOT_JOBS, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while((row = mysql_fetch_row(result))) {
		if(!period_start)
			period_start = atoi(row[JOB_REQ_SUBMIT]);

		memset(&job, 0, sizeof(local_job_t));

		job.account = row[JOB_REQ_ACCOUNT];
		job.alloc_cpus = row[JOB_REQ_ALLOC_CPUS];
		job.alloc_nodes = row[JOB_REQ_ALLOC_NODES];
		job.associd = row[JOB_REQ_ASSOCID];
		job.blockid = row[JOB_REQ_BLOCKID];
		job.comp_code = row[JOB_REQ_COMP_CODE];
		job.eligible = row[JOB_REQ_ELIGIBLE];
		job.end = row[JOB_REQ_END];
		job.gid = row[JOB_REQ_GID];
		job.id = row[JOB_REQ_ID];
		job.jobid = row[JOB_REQ_JOBID];
		job.kill_requid = row[JOB_REQ_KILL_REQUID];
		job.name = row[JOB_REQ_NAME];
		job.nodelist = row[JOB_REQ_NODELIST];
		job.node_inx = row[JOB_REQ_NODE_INX];
		job.partition = row[JOB_REQ_PARTITION];
		job.priority = row[JOB_REQ_PRIORITY];
		job.qos = row[JOB_REQ_QOS];
		job.req_cpus = row[JOB_REQ_REQ_CPUS];
		job.resvid = row[JOB_REQ_RESVID];
		job.start = row[JOB_REQ_START];
		job.state = row[JOB_REQ_STATE];
		job.submit = row[JOB_REQ_SUBMIT];
		job.suspended = row[JOB_REQ_SUSPENDED];
		job.track_steps = row[JOB_REQ_TRACKSTEPS];
		job.uid = row[JOB_REQ_UID];
		job.wckey = row[JOB_REQ_WCKEY];
		job.wckey_id = row[JOB_REQ_WCKEYID];

		_pack_local_job(&job, SLURMDBD_VERSION, buffer);
	}
	mysql_free_result(result);

//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);

	error_code = _write_archive_file(buffer, cluster_name,
					 period_start, period_end,
					 arch_dir, "job");
	free_buf(buffer);

	if(error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_jobs(uint16_t rpc_version, Buf buffer,
			char *cluster_name, uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL;
	local_job_t object;
	int i = 0;

	xstrfmtcat(insert, "insert into %s (%s", job_table, job_req_inx[0]);
	xstrcat(format, "('%s'");
	for(i=1; i<JOB_REQ_COUNT; i++) {
		xstrfmtcat(insert, ", %s", job_req_inx[i]);
		xstrcat(format, ", '%s'");
	}
	xstrcat(insert, ") values ");
	xstrcat(format, ")");
	for(i=0; i<rec_cnt; i++) {
		memset(&object, 0, sizeof(local_job_t));
		if(_unpack_local_job(&object, rpc_version, buffer)
		   != SLURM_SUCCESS) {
			error("issue unpacking");
			xfree(format);
			xfree(insert);
			break;
		}
		if(i)
			xstrcat(insert, ", ");

		xstrfmtcat(insert, format,
			   object.account,
			   object.alloc_cpus,
			   object.alloc_nodes,
			   object.associd,
			   object.blockid,
			   object.comp_code,
			   object.eligible,
			   object.end,
			   object.gid,
			   object.id,
			   object.jobid,
			   object.kill_requid,
			   object.name,
			   object.nodelist,
			   object.node_inx,
			   object.partition,
			   object.priority,
			   object.qos,
			   object.req_cpus,
			   object.resvid,
			   object.start,
			   object.state,
			   object.submit,
			   object.suspended,
			   object.track_steps,
			   object.uid,
			   object.wckey,
			   object.wckey_id);

	}
//	END_TIMER2("step query");
//	info("job query took %s", TIME_STR);
	xfree(format);

	return insert;
}

/* returns count of steps archived or SLURM_ERROR on error */
static uint32_t _archive_steps(mysql_conn_t *mysql_conn, char *cluster_name,
			       time_t period_end, char *arch_dir)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	local_step_t step;
	Buf buffer;
	int error_code = 0, i = 0;

	xfree(tmp);
	xstrfmtcat(tmp, "%s", step_req_inx[0]);
	for(i=1; i<STEP_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", step_req_inx[i]);
	}

	/* get all the events started before this time listed */
	query = xstrdup_printf("select %s from %s where "
			       "start <= %d && end != 0 "
			       "&& !deleted order by start asc",
			       tmp, step_table, period_end);
	xfree(tmp);

//	START_TIMER;
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if(!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = init_buf(high_buffer_size);
	pack16(SLURMDBD_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_STEP_START, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while((row = mysql_fetch_row(result))) {
		if(!period_start)
			period_start = atoi(row[STEP_REQ_START]);

		memset(&step, 0, sizeof(local_step_t));

		step.ave_cpu = row[STEP_REQ_AVE_CPU];
		step.ave_pages = row[STEP_REQ_AVE_PAGES];
		step.ave_rss = row[STEP_REQ_AVE_RSS];
		step.ave_vsize = row[STEP_REQ_AVE_VSIZE];
		step.comp_code = row[STEP_REQ_COMP_CODE];
		step.cpus = row[STEP_REQ_CPUS];
		step.id = row[STEP_REQ_ID];
		step.kill_requid = row[STEP_REQ_KILL_REQUID];
		step.max_pages = row[STEP_REQ_MAX_PAGES];
		step.max_pages_node = row[STEP_REQ_MAX_PAGES_NODE];
		step.max_pages_task = row[STEP_REQ_MAX_PAGES_TASK];
		step.max_rss = row[STEP_REQ_MAX_RSS];
		step.max_rss_node = row[STEP_REQ_MAX_RSS_NODE];
		step.max_rss_task = row[STEP_REQ_MAX_RSS_TASK];
		step.max_vsize = row[STEP_REQ_MAX_VSIZE];
		step.max_vsize_node = row[STEP_REQ_MAX_VSIZE_NODE];
		step.max_vsize_task = row[STEP_REQ_MAX_VSIZE_TASK];
		step.min_cpu = row[STEP_REQ_MIN_CPU];
		step.min_cpu_node = row[STEP_REQ_MIN_CPU_NODE];
		step.min_cpu_task = row[STEP_REQ_MIN_CPU_TASK];
		step.name = row[STEP_REQ_NAME];
		step.nodelist = row[STEP_REQ_NODELIST];
		step.nodes = row[STEP_REQ_NODES];
		step.node_inx = row[STEP_REQ_NODE_INX];
		step.period_end = row[STEP_REQ_END];
		step.period_start = row[STEP_REQ_START];
		step.period_suspended = row[STEP_REQ_SUSPENDED];
		step.state = row[STEP_REQ_STATE];
		step.stepid = row[STEP_REQ_STEPID];
		step.sys_sec = row[STEP_REQ_SYS_SEC];
		step.sys_usec = row[STEP_REQ_SYS_USEC];
		step.tasks = row[STEP_REQ_TASKS];
		step.task_dist = row[STEP_REQ_TASKDIST];
		step.user_sec = row[STEP_REQ_USER_SEC];
		step.user_usec = row[STEP_REQ_USER_USEC];

		_pack_local_step(&step, SLURMDBD_VERSION, buffer);
	}
	mysql_free_result(result);

//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);

	error_code = _write_archive_file(buffer, cluster_name,
					 period_start, period_end,
					 arch_dir, "event");
	free_buf(buffer);

	if(error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_steps(uint16_t rpc_version, Buf buffer,
			 char *cluster_name, uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL;
	local_step_t object;
	int i = 0;

	xstrfmtcat(insert, "insert into %s (%s", step_table, step_req_inx[0]);
	xstrcat(format, "('%s'");
	for(i=1; i<STEP_REQ_COUNT; i++) {
		xstrfmtcat(insert, ", %s", step_req_inx[i]);
		xstrcat(format, ", '%s'");
	}
	xstrcat(insert, ") values ");
	xstrcat(format, ")");
	for(i=0; i<rec_cnt; i++) {
		memset(&object, 0, sizeof(local_step_t));
		if(_unpack_local_step(&object, rpc_version, buffer)
		   != SLURM_SUCCESS) {
			error("issue unpacking");
			xfree(format);
			xfree(insert);
			break;
		}
		if(i)
			xstrcat(insert, ", ");

		xstrfmtcat(insert, format,
			   object.ave_cpu,
			   object.ave_pages,
			   object.ave_rss,
			   object.ave_vsize,
			   object.comp_code,
			   object.cpus,
			   object.id,
			   object.kill_requid,
			   object.max_pages,
			   object.max_pages_node,
			   object.max_pages_task,
			   object.max_rss,
			   object.max_rss_node,
			   object.max_rss_task,
			   object.max_vsize,
			   object.max_vsize_node,
			   object.max_vsize_task,
			   object.min_cpu,
			   object.min_cpu_node,
			   object.min_cpu_task,
			   object.name,
			   object.nodelist,
			   object.nodes,
			   object.node_inx,
			   object.period_end,
			   object.period_start,
			   object.period_suspended,
			   object.state,
			   object.stepid,
			   object.sys_sec,
			   object.sys_usec,
			   object.tasks,
			   object.task_dist,
			   object.user_sec,
			   object.user_usec);

	}
//	END_TIMER2("step query");
//	info("step query took %s", TIME_STR);
	xfree(format);

	return insert;
}

/* returns count of events archived or SLURM_ERROR on error */
static uint32_t _archive_suspend(mysql_conn_t *mysql_conn, char *cluster_name,
				 time_t period_end, char *arch_dir)
{
	MYSQL_RES *result = NULL;
	MYSQL_ROW row;
	char *tmp = NULL, *query = NULL;
	time_t period_start = 0;
	uint32_t cnt = 0;
	local_suspend_t suspend;
	Buf buffer;
	int error_code = 0, i = 0;

	xfree(tmp);
	xstrfmtcat(tmp, "%s", suspend_req_inx[0]);
	for(i=1; i<SUSPEND_REQ_COUNT; i++) {
		xstrfmtcat(tmp, ", %s", suspend_req_inx[i]);
	}

	/* get all the events started before this time listed */
	query = xstrdup_printf("select %s from %s where "
			       "start <= %d && end != 0 "
			       "order by start asc",
			       tmp, suspend_table, period_end);
	xfree(tmp);

//	START_TIMER;
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, query);
	if(!(result = mysql_db_query_ret(mysql_conn->db_conn, query, 0))) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);

	if(!(cnt = mysql_num_rows(result))) {
		mysql_free_result(result);
		return 0;
	}

	buffer = init_buf(high_buffer_size);
	pack16(SLURMDBD_VERSION, buffer);
	pack_time(time(NULL), buffer);
	pack16(DBD_JOB_SUSPEND, buffer);
	packstr(cluster_name, buffer);
	pack32(cnt, buffer);

	while((row = mysql_fetch_row(result))) {
		if(!period_start)
			period_start = atoi(row[SUSPEND_REQ_START]);

		memset(&suspend, 0, sizeof(local_suspend_t));

		suspend.id = row[SUSPEND_REQ_ID];
		suspend.associd = row[SUSPEND_REQ_ASSOCID];
		suspend.period_start = row[SUSPEND_REQ_START];
		suspend.period_end = row[SUSPEND_REQ_END];

		_pack_local_suspend(&suspend, SLURMDBD_VERSION, buffer);
	}
	mysql_free_result(result);

//	END_TIMER2("step query");
//	info("event query took %s", TIME_STR);

	error_code = _write_archive_file(buffer, cluster_name,
					 period_start, period_end,
					 arch_dir, "suspend");
	free_buf(buffer);

	if(error_code != SLURM_SUCCESS)
		return error_code;

	return cnt;
}

/* returns sql statement from archived data or NULL on error */
static char *_load_suspend(uint16_t rpc_version, Buf buffer,
			 char *cluster_name, uint32_t rec_cnt)
{
	char *insert = NULL, *format = NULL;
	local_suspend_t object;
	int i = 0;

	xstrfmtcat(insert, "insert into %s (%s",
		   suspend_table, suspend_req_inx[0]);
	xstrcat(format, "('%s'");
	for(i=1; i<SUSPEND_REQ_COUNT; i++) {
		xstrfmtcat(insert, ", %s", suspend_req_inx[i]);
		xstrcat(format, ", '%s'");
	}
	xstrcat(insert, ") values ");
	xstrcat(format, ")");
	for(i=0; i<rec_cnt; i++) {
		memset(&object, 0, sizeof(local_suspend_t));
		if(_unpack_local_suspend(&object, rpc_version, buffer)
		   != SLURM_SUCCESS) {
			error("issue unpacking");
			xfree(format);
			xfree(insert);
			break;
		}
		if(i)
			xstrcat(insert, ", ");

		xstrfmtcat(insert, format,
			   object.associd,
			   object.id,
			   object.period_end,
			   object.period_start);

	}
//	END_TIMER2("suspend query");
//	info("suspend query took %s", TIME_STR);
	xfree(format);

	return insert;
}

static int _archive_script(acct_archive_cond_t *arch_cond, char *cluster_name,
			   time_t last_submit)
{
	char * args[] = {arch_cond->archive_script, NULL};
	const char *tmpdir;
	struct stat st;
	char **env = NULL;
	struct tm time_tm;
	time_t curr_end;

#ifdef _PATH_TMP
	tmpdir = _PATH_TMP;
#else
	tmpdir = "/tmp";
#endif
	if (stat(arch_cond->archive_script, &st) < 0) {
		errno = errno;
		error("mysql_jobacct_process_run_script: failed to stat %s: %m",
		      arch_cond->archive_script);
		return SLURM_ERROR;
	}

	if (!(st.st_mode & S_IFREG)) {
		errno = EACCES;
		error("mysql_jobacct_process_run_script: "
		      "%s isn't a regular file",
		      arch_cond->archive_script);
		return SLURM_ERROR;
	}

	if (access(arch_cond->archive_script, X_OK) < 0) {
		errno = EACCES;
		error("mysql_jobacct_process_run_script: "
		      "%s is not executable", arch_cond->archive_script);
		return SLURM_ERROR;
	}

	env = env_array_create();
	env_array_append_fmt(&env, "SLURM_ARCHIVE_CLUSTER", "%s",
			     cluster_name);

	if(arch_cond->purge_event) {
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from "
			      "first event start %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_mon -= arch_cond->purge_step;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_EVENTS", "%u",
				     arch_cond->archive_events);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_LAST_EVENT", "%d",
				     curr_end);
	}

	if(arch_cond->purge_job) {
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first start %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_mon -= arch_cond->purge_job;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);

		env_array_append_fmt(&env, "SLURM_ARCHIVE_JOBS", "%u",
				     arch_cond->archive_jobs);
		env_array_append_fmt (&env, "SLURM_ARCHIVE_LAST_JOB", "%d",
				      curr_end);
	}

	if(arch_cond->purge_step) {
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first step start %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_mon -= arch_cond->purge_step;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_STEPS", "%u",
				     arch_cond->archive_steps);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_LAST_STEP", "%d",
				     curr_end);
	}

	if(arch_cond->purge_suspend) {
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first "
			      "suspend start %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_mon -= arch_cond->purge_step;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_SUSPEND", "%u",
				     arch_cond->archive_steps);
		env_array_append_fmt(&env, "SLURM_ARCHIVE_LAST_SUSPEND", "%d",
				     curr_end);
	}

#ifdef _PATH_STDPATH
	env_array_append (&env, "PATH", _PATH_STDPATH);
#else
	env_array_append (&env, "PATH", "/bin:/usr/bin");
#endif
	execve(arch_cond->archive_script, args, env);

	env_array_free(env);

	return SLURM_SUCCESS;
}

static int _execute_archive(mysql_conn_t *mysql_conn, time_t last_submit,
			    char *cluster_name, acct_archive_cond_t *arch_cond)
{
	int rc = SLURM_SUCCESS;
	char *query = NULL;
	time_t curr_end;
	struct tm time_tm;

	if(arch_cond->archive_script)
		return _archive_script(arch_cond, cluster_name, last_submit);
	else if(!arch_cond->archive_dir) {
		error("No archive dir given, can't process");
		return SLURM_ERROR;
	}

	if(arch_cond->purge_event) {
		/* remove all data from step table that was older than
		 * period_start * arch_cond->purge_event.
		 */
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first submit %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_sec = 0;
		time_tm.tm_min = 0;
		time_tm.tm_hour = 0;
		time_tm.tm_mday = 1;
		time_tm.tm_mon -= arch_cond->purge_event;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		curr_end--;

		debug4("from %d - %d months purging events from before %d",
		       last_submit, arch_cond->purge_event, curr_end);

		if(arch_cond->archive_events) {
			rc = _archive_events(mysql_conn, cluster_name,
					     curr_end, arch_cond->archive_dir);
			if(!rc)
				goto exit_events;
			else if(rc == SLURM_ERROR)
				return rc;
		}
		query = xstrdup_printf("delete from %s where "
				       "period_start <= %d && period_end != 0",
				       event_table, curr_end);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't remove old event data");
			return SLURM_ERROR;
		}
	}

exit_events:

	if(arch_cond->purge_suspend) {
		/* remove all data from step table that was older than
		 * period_start * arch_cond->purge_suspend.
		 */
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first submit %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_sec = 0;
		time_tm.tm_min = 0;
		time_tm.tm_hour = 0;
		time_tm.tm_mday = 1;
		time_tm.tm_mon -= arch_cond->purge_suspend;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		curr_end--;

		debug4("from %d - %d months purging suspend from before %d",
		       last_submit, arch_cond->purge_suspend, curr_end);

		if(arch_cond->archive_suspend) {
			rc = _archive_suspend(mysql_conn, cluster_name,
					      curr_end, arch_cond->archive_dir);
			if(!rc)
				goto exit_suspend;
			else if(rc == SLURM_ERROR)
				return rc;
		}
		query = xstrdup_printf("delete from %s where start <= %d "
				       "&& end != 0",
				       suspend_table, curr_end);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't remove old suspend data");
			return SLURM_ERROR;
		}
	}

exit_suspend:

	if(arch_cond->purge_step) {
		/* remove all data from step table that was older than
		 * start * arch_cond->purge_step.
		 */
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first start %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_sec = 0;
		time_tm.tm_min = 0;
		time_tm.tm_hour = 0;
		time_tm.tm_mday = 1;
		time_tm.tm_mon -= arch_cond->purge_step;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		curr_end--;

		debug4("from %d - %d months purging steps from before %d",
		       last_submit, arch_cond->purge_step, curr_end);

		if(arch_cond->archive_steps) {
			rc = _archive_steps(mysql_conn, cluster_name,
					    curr_end, arch_cond->archive_dir);
			if(!rc)
				goto exit_steps;
			else if(rc == SLURM_ERROR)
				return rc;
		}

		query = xstrdup_printf("delete from %s where start <= %d "
				       "&& end != 0",
				       step_table, curr_end);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't remove old step data");
			return SLURM_ERROR;
		}
	}
exit_steps:

	if(arch_cond->purge_job) {
		/* remove all data from step table that was older than
		 * last_submit * arch_cond->purge_job.
		 */
		/* use localtime to avoid any daylight savings issues */
		if(!localtime_r(&last_submit, &time_tm)) {
			error("Couldn't get localtime from first submit %d",
			      last_submit);
			return SLURM_ERROR;
		}
		time_tm.tm_sec = 0;
		time_tm.tm_min = 0;
		time_tm.tm_hour = 0;
		time_tm.tm_mday = 1;
		time_tm.tm_mon -= arch_cond->purge_job;
		time_tm.tm_isdst = -1;
		curr_end = mktime(&time_tm);
		curr_end--;

		debug4("from %d - %d months purging jobs from before %d",
		       last_submit, arch_cond->purge_job, curr_end);

		if(arch_cond->archive_jobs) {
			rc = _archive_jobs(mysql_conn, cluster_name,
					   curr_end, arch_cond->archive_dir);
			if(!rc)
				goto exit_jobs;
			else if(rc == SLURM_ERROR)
				return rc;
		}

		query = xstrdup_printf("delete from %s where submit <= %d "
				       "&& end != 0",
				       job_table, curr_end);
		debug3("%d(%s:%d) query\n%s",
		       mysql_conn->conn, __FILE__, __LINE__, query);
		rc = mysql_db_query(mysql_conn->db_conn, query);
		xfree(query);
		if(rc != SLURM_SUCCESS) {
			error("Couldn't remove old job data");
			return SLURM_ERROR;
		}
	}
exit_jobs:
	return SLURM_SUCCESS;
}

extern int mysql_jobacct_process_archive(mysql_conn_t *mysql_conn,
					 acct_archive_cond_t *arch_cond)
{
	int rc = SLURM_SUCCESS;
	char *cluster_name = NULL;
	time_t last_submit = time(NULL);
	struct tm time_tm;
	List use_cluster_list = mysql_cluster_list;
	ListIterator itr = NULL;

//	DEF_TIMERS;

	if(!arch_cond) {
		error("No arch_cond was given to archive from.  returning");
		return SLURM_ERROR;
	}

	if(!localtime_r(&last_submit, &time_tm)) {
		error("Couldn't get localtime from first start %d",
		      last_submit);
		return SLURM_ERROR;
	}
	time_tm.tm_sec = 0;
	time_tm.tm_min = 0;
	time_tm.tm_hour = 0;
	time_tm.tm_mday = 1;
	time_tm.tm_isdst = -1;
	last_submit = mktime(&time_tm);
	last_submit--;
	debug("archive: adjusted last submit is (%d)", last_submit);


	if(arch_cond->job_cond && arch_cond->job_cond->cluster_list
	   && list_count(arch_cond->job_cond->cluster_list))
		use_cluster_list = arch_cond->job_cond->cluster_list;
	else
		slurm_mutex_lock(&mysql_cluster_list_lock);

	itr = list_iterator_create(use_cluster_list);
	while((cluster_name = list_next(itr))) {
		if((rc = _execute_archive(mysql_conn, last_submit,
					  cluster_name, arch_cond))
		   != SLURM_SUCCESS)
			break;
	}
	list_iterator_destroy(itr);
	if(use_cluster_list == mysql_cluster_list)
		slurm_mutex_unlock(&mysql_cluster_list_lock);


	return rc;
}

extern int mysql_jobacct_process_archive_load(mysql_conn_t *mysql_conn,
					      acct_archive_rec_t *arch_rec)
{
	char *data = NULL, *cluster_name = NULL;
	int error_code = SLURM_SUCCESS;
	Buf buffer;
	time_t buf_time;
	uint16_t type = 0, ver = 0;
	uint32_t data_size = 0, rec_cnt = 0, tmp32 = 0;

	if(!arch_rec) {
		error("We need a acct_archive_rec to load anything.");
		return SLURM_ERROR;
	}

	if(arch_rec->insert) {
		data = xstrdup(arch_rec->insert);
	} else if(arch_rec->archive_file) {
		int data_allocated, data_read = 0;
		int state_fd = open(arch_rec->archive_file, O_RDONLY);
		if (state_fd < 0) {
			info("No archive file (%s) to recover",
			     arch_rec->archive_file);
			error_code = ENOENT;
		} else {
			data_allocated = BUF_SIZE;
			data = xmalloc(data_allocated);
			while (1) {
				data_read = read(state_fd, &data[data_size],
						 BUF_SIZE);
				if (data_read < 0) {
					if (errno == EINTR)
						continue;
					else {
						error("Read error on %s: %m",
						      arch_rec->archive_file);
						break;
					}
				} else if (data_read == 0)	/* eof */
					break;
				data_size      += data_read;
				data_allocated += data_read;
				xrealloc(data, data_allocated);
			}
			close(state_fd);
		}
		if(error_code != SLURM_SUCCESS) {
			xfree(data);
			return error_code;
		}
	} else {
		error("Nothing was set in your "
		      "acct_archive_rec so I am unable to process.");
		return SLURM_ERROR;
	}

	if(!data) {
		error("It doesn't appear we have anything to load.");
		return SLURM_ERROR;
	}

	/* this is the old version of an archive file where the file
	   was straight sql. */
	if((strlen(data) >= 12) &&
	   (!strncmp("insert into ", data, 12)
	    || (!strncmp("delete from ", data, 12))))
		goto got_sql;

	buffer = create_buf(data, data_size);

	safe_unpack16(&ver, buffer);
	debug3("Version in assoc_mgr_state header is %u", ver);
	if (ver <= SLURMDBD_VERSION || ver < SLURMDBD_VERSION_MIN) {
		error("***********************************************");
		error("Can not recover archive file, incompatible version, "
		      "got %u need > %u <= %u", ver,
		      SLURMDBD_VERSION_MIN, SLURMDBD_VERSION);
		error("***********************************************");
		free_buf(buffer);
		return EFAULT;
	}
	safe_unpack_time(&buf_time, buffer);
	safe_unpack16(&type, buffer);
	unpackstr_ptr(&cluster_name, &tmp32, buffer);
	safe_unpack32(&rec_cnt, buffer);

	if(!rec_cnt) {
		error("we didn't get any records from this file of type '%s'",
		      slurmdbd_msg_type_2_str(type, 0));
		free_buf(buffer);
		goto got_sql;
	}

	switch(type) {
	case DBD_GOT_EVENTS:
		data = _load_events(ver, buffer, cluster_name, rec_cnt);
		break;
	case DBD_GOT_JOBS:
		data = _load_jobs(ver, buffer, cluster_name, rec_cnt);
		break;
	case DBD_STEP_START:
		data = _load_steps(ver, buffer, cluster_name, rec_cnt);
		break;
	case DBD_JOB_SUSPEND:
		data = _load_suspend(ver, buffer, cluster_name, rec_cnt);
		break;
	default:
		error("Unknown type '%u' to load from archive", type);
		break;
	}
	free_buf(buffer);

got_sql:
	if(!data) {
		error("No data to load");
		return SLURM_ERROR;
	}
	debug3("%d(%s:%d) query\n%s",
	       mysql_conn->conn, __FILE__, __LINE__, data);
	error_code = mysql_db_query_check_after(mysql_conn->db_conn, data);
	xfree(data);
	if(error_code != SLURM_SUCCESS) {
	unpack_error:
		error("Couldn't load old data");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
