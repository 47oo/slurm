/*****************************************************************************\
 *  src/common/env.h - environment vector manipulation
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/
#ifndef _ENV_H
#define _ENV_H

#include <sys/types.h>
#include <unistd.h>
#include <slurm/slurm.h>
#include <sys/utsname.h>

#include "src/common/macros.h"

typedef struct env_options {
	int nprocs;		/* --nprocs=n,      -n n	*/
	char *task_count;
	bool nprocs_set;	/* true if nprocs explicitly set */
	bool cpus_set;		/* true if cpus_per_task explicitly set */
	enum task_dist_states
		distribution;	/* --distribution=, -m dist	*/
	cpu_bind_type_t
		cpu_bind_type;	/* --cpu_bind=			*/
	char *cpu_bind;		/* binding map for map/mask_cpu	*/
	mem_bind_type_t
		mem_bind_type;	/* --mem_bind=			*/
	char *mem_bind;		/* binding map for tasks to memory	*/
	bool overcommit;	/* --overcommit,   -O		*/
	int  slurmd_debug;	/* --slurmd-debug, -D           */
	bool labelio;		/* --label-output, -l		*/
	select_jobinfo_t select_jobinfo;
	int nhosts;
	char *nodelist;		/* nodelist in string form */
	char **env;             /* job environment */
	uint16_t comm_port;	/* srun's communication port */
	char *comm_hostname;	/* srun's hostname */
	slurm_addr *cli;	/* launch node address */
	slurm_addr *self;
	int jobid;		/* assigned job id */
	int stepid;	        /* assigned step id */
	int procid;		/* global task id (across nodes) */
	int localid;		/* local task id (within node) */
	int nodeid;
	int cpus_per_task;	/* --cpus-per-task=n, -c n	*/
	int cpus_on_node;
	pid_t task_pid;
} env_t;


int     envcount (char **env);
int     setenvfs(const char *fmt, ...);
int     setenvf(char ***envp, const char *name, const char *fmt, ...);
void	unsetenvp(char **env, const char *name);
char *	getenvp(char **env, const char *name);
int     setup_env(env_t *env);

/**********************************************************************
 * Newer environment variable handling scheme
 **********************************************************************/
/*
 * Create an array of pointers to environment variables strings relevant
 * to a SLURM job allocation.  The array is terminated by a NULL pointer,
 * and thus is suitable for use by execle() and other env_array_* functions.
 *
 * Sets the variables:
 *	SLURM_JOB_ID
 *	SLURM_JOB_NUM_NODES
 *	SLURM_JOB_NODELIST
 *	SLURM_JOB_CPUS_PER_NODE
 *
 * Sets OBSOLETE variables:
 *	? probably only needed for users...
 */
char **
env_array_create_for_job(const resource_allocation_response_msg_t *alloc);

/*
 * Create an array of pointers to environment variables strings relevant
 * to a SLURM job step.  The array is terminated by a NULL pointer,
 * and thus is suitable for use by execle() and other env_array_* functions.
 *
 * Sets variables:
 *	SLURM_STEP_ID
 *	SLURM_STEP_NUM_NODES
 *	SLURM_STEP_NUM_TASKS
 *	SLURM_STEP_TASKS_PER_NODE
 *	SLURM_STEP_LAUNCHER_HOSTNAME
 *	SLURM_STEP_LAUNCHER_PORT
 *	SLURM_STEP_LAUNCHER_IPADDR
 *
 * Sets OBSOLETE variables:
 *	SLURM_STEPID
 *      SLURM_NNODES
 *	SLURM_NPROCS
 *	SLURM_NODELIST
 *	SLURM_TASKS_PER_NODE
 *	SLURM_SRUN_COMM_HOST
 *	SLURM_SRUN_COMM_PORT
 *	SLURM_LAUNCH_NODE_IPADDR
 *
 */
char **
env_array_create_for_step(const job_step_create_response_msg_t *step,
			  const char *launcher_hostname,
			  uint16_t launcher_port,
			  const char *ip_addr_str);

/*
 * Return an empty environment variable array (contains a single
 * pointer to NULL).
 */
char **env_array_create(void);

/*
 * Merge all of the environment variables in src_array into the
 * array dest_array.  Any variables already found in dest_array
 * will be overwritten with the value from src_array.
 */
void env_array_merge(char ***dest_array, const char **src_array);

/*
 * Free the memory used by an environment variable array.
 */
void env_array_free(char **env_array);

/*
 * Append a single environment variable to an environment variable array,
 * if and only if a variable by that name does not already exist in the
 * array.
 *
 * Return 1 on success, and 0 on error.
 */
int env_array_append(char ***array_ptr, const char *name,
		     const char *value_fmt, ...);

/*
 * Append a single environment variable to an environment variable array
 * if a variable by that name does not already exist.  If a variable
 * by the same name is found in the array, it is overwritten with the
 * new value.
 *
 * Return 1 on success, and 0 on error.
 */
int env_array_overwrite(char ***array_ptr, const char *name,
			const char *value_fmt, ...);

/*
 * Set all of the environment variables in a supplied environment
 * variable array.
 */
void env_array_set_environment(char **env_array);

#endif
