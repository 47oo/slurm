/*****************************************************************************\
 *  slurmdb_defs.c - definitions used by slurmdb api
 ******************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble da@llnl.gov, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <stdlib.h>

#include "src/common/slurmdb_defs.h"
#include "src/common/assoc_mgr.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_strcasestr.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/parse_time.h"

/*
 * Comparator used for sorting immediate childern of acct_hierarchical_recs
 *
 * returns: -1: assoc_a > assoc_b   0: assoc_a == assoc_b   1: assoc_a < assoc_b
 *
 */

static int _sort_childern_list(slurmdb_hierarchical_rec_t *assoc_a,
			       slurmdb_hierarchical_rec_t *assoc_b)
{
	int diff = 0;

	/* first just check the lfts and rgts if a lft is inside of the
	 * others lft and rgt just return it is less
	 */
	if(assoc_a->assoc->lft > assoc_b->assoc->lft
	   && assoc_a->assoc->lft < assoc_b->assoc->rgt)
		return 1;

	/* check to see if this is a user association or an account.
	 * We want the accounts at the bottom
	 */
	if(assoc_a->assoc->user && !assoc_b->assoc->user)
		return -1;
	else if(!assoc_a->assoc->user && assoc_b->assoc->user)
		return 1;

	diff = strcmp(assoc_a->sort_name, assoc_b->sort_name);

	if (diff < 0)
		return -1;
	else if (diff > 0)
		return 1;

	return 0;

}

static int _sort_slurmdb_hierarchical_rec_list(
	List slurmdb_hierarchical_rec_list)
{
	slurmdb_hierarchical_rec_t *slurmdb_hierarchical_rec = NULL;
	ListIterator itr;

	if(!list_count(slurmdb_hierarchical_rec_list))
		return SLURM_SUCCESS;

	list_sort(slurmdb_hierarchical_rec_list, (ListCmpF)_sort_childern_list);

	itr = list_iterator_create(slurmdb_hierarchical_rec_list);
	while((slurmdb_hierarchical_rec = list_next(itr))) {
		if(list_count(slurmdb_hierarchical_rec->childern))
			_sort_slurmdb_hierarchical_rec_list(
				slurmdb_hierarchical_rec->childern);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static int _append_hierarchical_childern_ret_list(
	List ret_list, List slurmdb_hierarchical_rec_list)
{
	slurmdb_hierarchical_rec_t *slurmdb_hierarchical_rec = NULL;
	ListIterator itr;

	if(!ret_list)
		return SLURM_ERROR;

	if(!list_count(slurmdb_hierarchical_rec_list))
		return SLURM_SUCCESS;

	itr = list_iterator_create(slurmdb_hierarchical_rec_list);
	while((slurmdb_hierarchical_rec = list_next(itr))) {
		list_append(ret_list, slurmdb_hierarchical_rec->assoc);

		if(list_count(slurmdb_hierarchical_rec->childern))
			_append_hierarchical_childern_ret_list(
				ret_list, slurmdb_hierarchical_rec->childern);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

extern slurmdb_job_rec_t *slurmdb_create_job_rec()
{
	slurmdb_job_rec_t *job = xmalloc(sizeof(slurmdb_job_rec_t));
	memset(&job->stats, 0, sizeof(slurmdb_stats_t));
	job->stats.cpu_min = NO_VAL;
	job->state = JOB_PENDING;
	job->steps = list_create(slurmdb_destroy_step_rec);
	job->requid = -1;
	job->lft = (uint32_t)NO_VAL;
	job->resvid = (uint32_t)NO_VAL;

      	return job;
}

extern slurmdb_step_rec_t *slurmdb_create_step_rec()
{
	slurmdb_step_rec_t *step = xmalloc(sizeof(slurmdb_job_rec_t));
	memset(&step->stats, 0, sizeof(slurmdb_stats_t));
	step->stepid = (uint32_t)NO_VAL;
	step->state = NO_VAL;
	step->exitcode = NO_VAL;
	step->ncpus = (uint32_t)NO_VAL;
	step->elapsed = (uint32_t)NO_VAL;
	step->tot_cpu_sec = (uint32_t)NO_VAL;
	step->tot_cpu_usec = (uint32_t)NO_VAL;
	step->requid = -1;

	return step;
}


extern void slurmdb_destroy_user_rec(void *object)
{
	slurmdb_user_rec_t *slurmdb_user = (slurmdb_user_rec_t *)object;

	if(slurmdb_user) {
		if(slurmdb_user->assoc_list)
			list_destroy(slurmdb_user->assoc_list);
		if(slurmdb_user->coord_accts)
			list_destroy(slurmdb_user->coord_accts);
		xfree(slurmdb_user->default_acct);
		xfree(slurmdb_user->default_wckey);
		xfree(slurmdb_user->name);
		if(slurmdb_user->wckey_list)
			list_destroy(slurmdb_user->wckey_list);
		xfree(slurmdb_user);
	}
}

extern void slurmdb_destroy_account_rec(void *object)
{
	slurmdb_account_rec_t *slurmdb_account =
		(slurmdb_account_rec_t *)object;

	if(slurmdb_account) {
		if(slurmdb_account->assoc_list)
			list_destroy(slurmdb_account->assoc_list);
		if(slurmdb_account->coordinators)
			list_destroy(slurmdb_account->coordinators);
		xfree(slurmdb_account->description);
		xfree(slurmdb_account->name);
		xfree(slurmdb_account->organization);
		xfree(slurmdb_account);
	}
}

extern void slurmdb_destroy_coord_rec(void *object)
{
	slurmdb_coord_rec_t *slurmdb_coord =
		(slurmdb_coord_rec_t *)object;

	if(slurmdb_coord) {
		xfree(slurmdb_coord->name);
		xfree(slurmdb_coord);
	}
}

extern void slurmdb_destroy_cluster_accounting_rec(void *object)
{
	slurmdb_cluster_accounting_rec_t *clusteracct_rec =
		(slurmdb_cluster_accounting_rec_t *)object;

	if(clusteracct_rec) {
		xfree(clusteracct_rec);
	}
}

extern void slurmdb_destroy_cluster_rec(void *object)
{
	slurmdb_cluster_rec_t *slurmdb_cluster =
		(slurmdb_cluster_rec_t *)object;

	if(slurmdb_cluster) {
		if(slurmdb_cluster->accounting_list)
			list_destroy(slurmdb_cluster->accounting_list);
		xfree(slurmdb_cluster->control_host);
		xfree(slurmdb_cluster->name);
		xfree(slurmdb_cluster->nodes);
		slurmdb_destroy_association_rec(slurmdb_cluster->root_assoc);
		xfree(slurmdb_cluster);
	}
}

extern void slurmdb_destroy_accounting_rec(void *object)
{
	slurmdb_accounting_rec_t *slurmdb_accounting =
		(slurmdb_accounting_rec_t *)object;

	if(slurmdb_accounting) {
		xfree(slurmdb_accounting);
	}
}

extern void slurmdb_destroy_association_rec(void *object)
{
	slurmdb_association_rec_t *slurmdb_association =
		(slurmdb_association_rec_t *)object;

	if(slurmdb_association) {
		if(slurmdb_association->accounting_list)
			list_destroy(slurmdb_association->accounting_list);
		xfree(slurmdb_association->acct);
		xfree(slurmdb_association->cluster);
		xfree(slurmdb_association->parent_acct);
		xfree(slurmdb_association->partition);
		if(slurmdb_association->qos_list)
			list_destroy(slurmdb_association->qos_list);
		xfree(slurmdb_association->user);

		destroy_assoc_mgr_association_usage(slurmdb_association->usage);

		xfree(slurmdb_association);
	}
}

extern void slurmdb_destroy_event_rec(void *object)
{
	slurmdb_event_rec_t *slurmdb_event =
		(slurmdb_event_rec_t *)object;

	if(slurmdb_event) {
		xfree(slurmdb_event->cluster);
		xfree(slurmdb_event->cluster_nodes);
		xfree(slurmdb_event->node_name);
		xfree(slurmdb_event->reason);

		xfree(slurmdb_event);
	}
}

extern void slurmdb_destroy_job_rec(void *object)
{
	slurmdb_job_rec_t *job = (slurmdb_job_rec_t *)object;
	if (job) {
		xfree(job->account);
		xfree(job->blockid);
		xfree(job->cluster);
		xfree(job->jobname);
		xfree(job->partition);
		xfree(job->nodes);
		if(job->steps) {
			list_destroy(job->steps);
			job->steps = NULL;
		}
		xfree(job->user);
		xfree(job->wckey);
		xfree(job);
	}
}

extern void slurmdb_destroy_qos_rec(void *object)
{
	slurmdb_qos_rec_t *slurmdb_qos = (slurmdb_qos_rec_t *)object;
	if(slurmdb_qos) {
		xfree(slurmdb_qos->description);
		xfree(slurmdb_qos->name);
		FREE_NULL_BITMAP(slurmdb_qos->preempt_bitstr);
		if(slurmdb_qos->preempt_list)
			list_destroy(slurmdb_qos->preempt_list);
		destroy_assoc_mgr_qos_usage(slurmdb_qos->usage);
		xfree(slurmdb_qos);
	}
}

extern void slurmdb_destroy_reservation_rec(void *object)
{
	slurmdb_reservation_rec_t *slurmdb_resv =
		(slurmdb_reservation_rec_t *)object;
	if(slurmdb_resv) {
		xfree(slurmdb_resv->assocs);
		xfree(slurmdb_resv->cluster);
		xfree(slurmdb_resv->name);
		xfree(slurmdb_resv->nodes);
		xfree(slurmdb_resv->node_inx);
		xfree(slurmdb_resv);
	}
}

extern void slurmdb_destroy_step_rec(void *object)
{
	slurmdb_step_rec_t *step = (slurmdb_step_rec_t *)object;
	if (step) {
		xfree(step->nodes);
		xfree(step->stepname);
		xfree(step);
	}
}

extern void slurmdb_destroy_txn_rec(void *object)
{
	slurmdb_txn_rec_t *slurmdb_txn = (slurmdb_txn_rec_t *)object;
	if(slurmdb_txn) {
		xfree(slurmdb_txn->accts);
		xfree(slurmdb_txn->actor_name);
		xfree(slurmdb_txn->clusters);
		xfree(slurmdb_txn->set_info);
		xfree(slurmdb_txn->users);
		xfree(slurmdb_txn->where_query);
		xfree(slurmdb_txn);
	}
}

extern void slurmdb_destroy_wckey_rec(void *object)
{
	slurmdb_wckey_rec_t *wckey = (slurmdb_wckey_rec_t *)object;

	if(wckey) {
		if(wckey->accounting_list)
			list_destroy(wckey->accounting_list);
		xfree(wckey->cluster);
		xfree(wckey->name);
		xfree(wckey->user);
		xfree(wckey);
	}
}

extern void slurmdb_destroy_archive_rec(void *object)
{
	slurmdb_archive_rec_t *arch_rec = (slurmdb_archive_rec_t *)object;

	if(arch_rec) {
		xfree(arch_rec->archive_file);
		xfree(arch_rec->insert);
		xfree(arch_rec);
	}
}

extern void slurmdb_destroy_user_cond(void *object)
{
	slurmdb_user_cond_t *slurmdb_user = (slurmdb_user_cond_t *)object;

	if(slurmdb_user) {
		slurmdb_destroy_association_cond(slurmdb_user->assoc_cond);
		if(slurmdb_user->def_acct_list)
			list_destroy(slurmdb_user->def_acct_list);
		if(slurmdb_user->def_wckey_list)
			list_destroy(slurmdb_user->def_wckey_list);
		xfree(slurmdb_user);
	}
}

extern void slurmdb_destroy_account_cond(void *object)
{
	slurmdb_account_cond_t *slurmdb_account =
		(slurmdb_account_cond_t *)object;

	if(slurmdb_account) {
		slurmdb_destroy_association_cond(slurmdb_account->assoc_cond);
		if(slurmdb_account->description_list)
			list_destroy(slurmdb_account->description_list);
		if(slurmdb_account->organization_list)
			list_destroy(slurmdb_account->organization_list);
		xfree(slurmdb_account);
	}
}

extern void slurmdb_destroy_cluster_cond(void *object)
{
	slurmdb_cluster_cond_t *slurmdb_cluster =
		(slurmdb_cluster_cond_t *)object;

	if(slurmdb_cluster) {
		if(slurmdb_cluster->cluster_list)
			list_destroy(slurmdb_cluster->cluster_list);
		xfree(slurmdb_cluster);
	}
}

extern void slurmdb_destroy_association_cond(void *object)
{
	slurmdb_association_cond_t *slurmdb_association =
		(slurmdb_association_cond_t *)object;

	if(slurmdb_association) {
		if(slurmdb_association->acct_list)
			list_destroy(slurmdb_association->acct_list);
		if(slurmdb_association->cluster_list)
			list_destroy(slurmdb_association->cluster_list);

		if(slurmdb_association->fairshare_list)
			list_destroy(slurmdb_association->fairshare_list);

		if(slurmdb_association->grp_cpu_mins_list)
			list_destroy(slurmdb_association->grp_cpu_mins_list);
		if(slurmdb_association->grp_cpus_list)
			list_destroy(slurmdb_association->grp_cpus_list);
		if(slurmdb_association->grp_jobs_list)
			list_destroy(slurmdb_association->grp_jobs_list);
		if(slurmdb_association->grp_nodes_list)
			list_destroy(slurmdb_association->grp_nodes_list);
		if(slurmdb_association->grp_submit_jobs_list)
			list_destroy(slurmdb_association->grp_submit_jobs_list);
		if(slurmdb_association->grp_wall_list)
			list_destroy(slurmdb_association->grp_wall_list);

		if(slurmdb_association->id_list)
			list_destroy(slurmdb_association->id_list);

		if(slurmdb_association->max_cpu_mins_pj_list)
			list_destroy(slurmdb_association->max_cpu_mins_pj_list);
		if(slurmdb_association->max_cpus_pj_list)
			list_destroy(slurmdb_association->max_cpus_pj_list);
		if(slurmdb_association->max_jobs_list)
			list_destroy(slurmdb_association->max_jobs_list);
		if(slurmdb_association->max_nodes_pj_list)
			list_destroy(slurmdb_association->max_nodes_pj_list);
		if(slurmdb_association->max_submit_jobs_list)
			list_destroy(slurmdb_association->max_submit_jobs_list);
		if(slurmdb_association->max_wall_pj_list)
			list_destroy(slurmdb_association->max_wall_pj_list);

		if(slurmdb_association->partition_list)
			list_destroy(slurmdb_association->partition_list);

		if(slurmdb_association->parent_acct_list)
			list_destroy(slurmdb_association->parent_acct_list);

		if(slurmdb_association->qos_list)
			list_destroy(slurmdb_association->qos_list);
		if(slurmdb_association->user_list)
			list_destroy(slurmdb_association->user_list);
		xfree(slurmdb_association);
	}
}

extern void slurmdb_destroy_event_cond(void *object)
{
	slurmdb_event_cond_t *slurmdb_event =
		(slurmdb_event_cond_t *)object;

	if(slurmdb_event) {
		if(slurmdb_event->cluster_list)
			list_destroy(slurmdb_event->cluster_list);
		if(slurmdb_event->node_list)
			list_destroy(slurmdb_event->node_list);
		if(slurmdb_event->reason_list)
			list_destroy(slurmdb_event->reason_list);
		if(slurmdb_event->reason_uid_list)
			list_destroy(slurmdb_event->reason_uid_list);
		if(slurmdb_event->state_list)
			list_destroy(slurmdb_event->state_list);
		xfree(slurmdb_event);
	}
}

extern void slurmdb_destroy_job_cond(void *object)
{
	slurmdb_job_cond_t *job_cond =
		(slurmdb_job_cond_t *)object;

	if(job_cond) {
		if(job_cond->acct_list)
			list_destroy(job_cond->acct_list);
		if(job_cond->associd_list)
			list_destroy(job_cond->associd_list);
		if(job_cond->cluster_list)
			list_destroy(job_cond->cluster_list);
		if(job_cond->groupid_list)
			list_destroy(job_cond->groupid_list);
		if(job_cond->partition_list)
			list_destroy(job_cond->partition_list);
		if(job_cond->resv_list)
			list_destroy(job_cond->resv_list);
		if(job_cond->resvid_list)
			list_destroy(job_cond->resvid_list);
		if(job_cond->step_list)
			list_destroy(job_cond->step_list);
		if(job_cond->state_list)
			list_destroy(job_cond->state_list);
		xfree(job_cond->used_nodes);
		if(job_cond->userid_list)
			list_destroy(job_cond->userid_list);
		if(job_cond->wckey_list)
			list_destroy(job_cond->wckey_list);
		xfree(job_cond);
	}
}

extern void slurmdb_destroy_qos_cond(void *object)
{
	slurmdb_qos_cond_t *slurmdb_qos = (slurmdb_qos_cond_t *)object;
	if(slurmdb_qos) {
		if(slurmdb_qos->id_list)
			list_destroy(slurmdb_qos->id_list);
		if(slurmdb_qos->name_list)
			list_destroy(slurmdb_qos->name_list);
		xfree(slurmdb_qos);
	}
}

extern void slurmdb_destroy_reservation_cond(void *object)
{
	slurmdb_reservation_cond_t *slurmdb_resv =
		(slurmdb_reservation_cond_t *)object;
	if(slurmdb_resv) {
		if(slurmdb_resv->cluster_list)
			list_destroy(slurmdb_resv->cluster_list);
		if(slurmdb_resv->id_list)
			list_destroy(slurmdb_resv->id_list);
		if(slurmdb_resv->name_list)
			list_destroy(slurmdb_resv->name_list);
		xfree(slurmdb_resv->nodes);
		xfree(slurmdb_resv);
	}
}

extern void slurmdb_destroy_txn_cond(void *object)
{
	slurmdb_txn_cond_t *slurmdb_txn = (slurmdb_txn_cond_t *)object;
	if(slurmdb_txn) {
		if(slurmdb_txn->acct_list)
			list_destroy(slurmdb_txn->acct_list);
		if(slurmdb_txn->action_list)
			list_destroy(slurmdb_txn->action_list);
		if(slurmdb_txn->actor_list)
			list_destroy(slurmdb_txn->actor_list);
		if(slurmdb_txn->cluster_list)
			list_destroy(slurmdb_txn->cluster_list);
		if(slurmdb_txn->id_list)
			list_destroy(slurmdb_txn->id_list);
		if(slurmdb_txn->info_list)
			list_destroy(slurmdb_txn->info_list);
		if(slurmdb_txn->name_list)
			list_destroy(slurmdb_txn->name_list);
		if(slurmdb_txn->user_list)
			list_destroy(slurmdb_txn->user_list);
		xfree(slurmdb_txn);
	}
}

extern void slurmdb_destroy_wckey_cond(void *object)
{
	slurmdb_wckey_cond_t *wckey = (slurmdb_wckey_cond_t *)object;

	if(wckey) {
		if(wckey->cluster_list)
			list_destroy(wckey->cluster_list);
		if(wckey->id_list)
			list_destroy(wckey->id_list);
		if(wckey->name_list)
			list_destroy(wckey->name_list);
		if(wckey->user_list)
			list_destroy(wckey->user_list);
		xfree(wckey);
	}
}

extern void slurmdb_destroy_archive_cond(void *object)
{
	slurmdb_archive_cond_t *arch_cond = (slurmdb_archive_cond_t *)object;

	if(arch_cond) {
		xfree(arch_cond->archive_dir);
		xfree(arch_cond->archive_script);
		slurmdb_destroy_job_cond(arch_cond->job_cond);
		xfree(arch_cond);

	}
}

extern void slurmdb_destroy_update_object(void *object)
{
	slurmdb_update_object_t *slurmdb_update =
		(slurmdb_update_object_t *) object;

	if(slurmdb_update) {
		if(slurmdb_update->objects)
			list_destroy(slurmdb_update->objects);

		xfree(slurmdb_update);
	}
}

extern void slurmdb_destroy_used_limits(void *object)
{
	slurmdb_used_limits_t *slurmdb_used_limits =
		(slurmdb_used_limits_t *)object;

	if(slurmdb_used_limits) {
		xfree(slurmdb_used_limits);
	}
}

extern void slurmdb_destroy_update_shares_rec(void *object)
{
	xfree(object);
}

extern void slurmdb_destroy_print_tree(void *object)
{
	slurmdb_print_tree_t *slurmdb_print_tree =
		(slurmdb_print_tree_t *)object;

	if(slurmdb_print_tree) {
		xfree(slurmdb_print_tree->name);
		xfree(slurmdb_print_tree->print_name);
		xfree(slurmdb_print_tree->spaces);
		xfree(slurmdb_print_tree);
	}
}

extern void slurmdb_destroy_hierarchical_rec(void *object)
{
	/* Most of this is pointers to something else that will be
	 * destroyed elsewhere.
	 */
	slurmdb_hierarchical_rec_t *slurmdb_hierarchical_rec =
		(slurmdb_hierarchical_rec_t *)object;
	if(slurmdb_hierarchical_rec) {
		if(slurmdb_hierarchical_rec->childern) {
			list_destroy(slurmdb_hierarchical_rec->childern);
		}
		xfree(slurmdb_hierarchical_rec);
	}
}

extern void slurmdb_destroy_selected_step(void *object)
{
	slurmdb_selected_step_t *step = (slurmdb_selected_step_t *)object;
	if (step) {
		xfree(step);
	}
}

extern void slurmdb_init_association_rec(slurmdb_association_rec_t *assoc)
{
	if(!assoc)
		return;

	memset(assoc, 0, sizeof(slurmdb_association_rec_t));

	assoc->grp_cpu_mins = NO_VAL;
	assoc->grp_cpus = NO_VAL;
	assoc->grp_jobs = NO_VAL;
	assoc->grp_nodes = NO_VAL;
	assoc->grp_submit_jobs = NO_VAL;
	assoc->grp_wall = NO_VAL;

	/* assoc->level_shares = NO_VAL; */

	assoc->max_cpu_mins_pj = NO_VAL;
	assoc->max_cpus_pj = NO_VAL;
	assoc->max_jobs = NO_VAL;
	assoc->max_nodes_pj = NO_VAL;
	assoc->max_submit_jobs = NO_VAL;
	assoc->max_wall_pj = NO_VAL;

	/* assoc->shares_norm = (double)NO_VAL; */
	assoc->shares_raw = NO_VAL;

	/* assoc->usage_efctv = 0; */
	/* assoc->usage_norm = (long double)NO_VAL; */
	/* assoc->usage_raw = 0; */
}

extern void slurmdb_init_qos_rec(slurmdb_qos_rec_t *qos)
{
	if(!qos)
		return;

	memset(qos, 0, sizeof(slurmdb_qos_rec_t));

	qos->priority = NO_VAL;

	qos->grp_cpu_mins = NO_VAL;
	qos->grp_cpus = NO_VAL;
	qos->grp_jobs = NO_VAL;
	qos->grp_nodes = NO_VAL;
	qos->grp_submit_jobs = NO_VAL;
	qos->grp_wall = NO_VAL;

	qos->max_cpu_mins_pj = NO_VAL;
	qos->max_cpus_pj = NO_VAL;
	qos->max_jobs_pu = NO_VAL;
	qos->max_nodes_pj = NO_VAL;
	qos->max_submit_jobs_pu = NO_VAL;
	qos->max_wall_pj = NO_VAL;

	qos->usage_factor = NO_VAL;
}

extern char *slurmdb_qos_str(List qos_list, uint32_t level)
{
	ListIterator itr = NULL;
	slurmdb_qos_rec_t *qos = NULL;

	if(!qos_list) {
		error("We need a qos list to translate");
		return NULL;
	} else if(!level) {
		debug2("no level");
		return "";
	}

	itr = list_iterator_create(qos_list);
	while((qos = list_next(itr))) {
		if(level == qos->id)
			break;
	}
	list_iterator_destroy(itr);
	if(qos)
		return qos->name;
	else
		return NULL;
}

extern uint32_t str_2_slurmdb_qos(List qos_list, char *level)
{
	ListIterator itr = NULL;
	slurmdb_qos_rec_t *qos = NULL;
	char *working_level = NULL;

	if(!qos_list) {
		error("We need a qos list to translate");
		return NO_VAL;
	} else if(!level) {
		debug2("no level");
		return 0;
	}
	if(level[0] == '+' || level[0] == '-')
		working_level = level+1;
	else
		working_level = level;

	itr = list_iterator_create(qos_list);
	while((qos = list_next(itr))) {
		if(!strncasecmp(working_level, qos->name,
				strlen(working_level)))
			break;
	}
	list_iterator_destroy(itr);
	if(qos)
		return qos->id;
	else
		return NO_VAL;
}

extern char *slurmdb_admin_level_str(slurmdb_admin_level_t level)
{
	switch(level) {
	case SLURMDB_ADMIN_NOTSET:
		return "Not Set";
		break;
	case SLURMDB_ADMIN_NONE:
		return "None";
		break;
	case SLURMDB_ADMIN_OPERATOR:
		return "Operator";
		break;
	case SLURMDB_ADMIN_SUPER_USER:
		return "Administrator";
		break;
	default:
		return "Unknown";
		break;
	}
	return "Unknown";
}

extern slurmdb_admin_level_t str_2_slurmdb_admin_level(char *level)
{
	if(!level) {
		return SLURMDB_ADMIN_NOTSET;
	} else if(!strncasecmp(level, "None", 1)) {
		return SLURMDB_ADMIN_NONE;
	} else if(!strncasecmp(level, "Operator", 1)) {
		return SLURMDB_ADMIN_OPERATOR;
	} else if(!strncasecmp(level, "SuperUser", 1)
		  || !strncasecmp(level, "Admin", 1)) {
		return SLURMDB_ADMIN_SUPER_USER;
	} else {
		return SLURMDB_ADMIN_NOTSET;
	}
}

/* This reorders the list into a alphabetical hierarchy returned in a
 * separate list.  The orginal list is not affected */
extern List get_hierarchical_sorted_assoc_list(List assoc_list)
{
	List slurmdb_hierarchical_rec_list =
		get_slurmdb_hierarchical_rec_list(assoc_list);
	List ret_list = list_create(NULL);

	_append_hierarchical_childern_ret_list(ret_list,
					       slurmdb_hierarchical_rec_list);
	list_destroy(slurmdb_hierarchical_rec_list);

	return ret_list;
}

extern List get_slurmdb_hierarchical_rec_list(List assoc_list)
{
	slurmdb_hierarchical_rec_t *par_arch_rec = NULL;
	slurmdb_hierarchical_rec_t *last_slurmdb_parent = NULL;
	slurmdb_hierarchical_rec_t *last_parent = NULL;
	slurmdb_hierarchical_rec_t *arch_rec = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	List total_assoc_list = list_create(NULL);
	List arch_rec_list =
		list_create(slurmdb_destroy_hierarchical_rec);
	ListIterator itr, itr2;

	itr = list_iterator_create(assoc_list);
	itr2 = list_iterator_create(total_assoc_list);

	while((assoc = list_next(itr))) {
		arch_rec =
			xmalloc(sizeof(slurmdb_hierarchical_rec_t));
		arch_rec->childern =
			list_create(slurmdb_destroy_hierarchical_rec);
		arch_rec->assoc = assoc;

		/* To speed things up we are first looking if we have
		   a parent_id to look for.  If that doesn't work see
		   if the last parent we had was what we are looking
		   for.  Then if that isn't panning out look at the
		   last account parent.  If still we don't have it we
		   will look for it in the list.  If it isn't there we
		   will just add it to the parent and call it good
		*/
		if(!assoc->parent_id) {
			arch_rec->sort_name = assoc->cluster;

			list_append(arch_rec_list, arch_rec);
			list_append(total_assoc_list, arch_rec);

			continue;
		}

		if(assoc->user)
			arch_rec->sort_name = assoc->user;
		else
			arch_rec->sort_name = assoc->acct;

		if(last_parent && assoc->parent_id == last_parent->assoc->id) {
			par_arch_rec = last_parent;
		} else if(last_slurmdb_parent
			  && (assoc->parent_id ==
			      last_slurmdb_parent->assoc->id)) {
			par_arch_rec = last_slurmdb_parent;
		} else {
			list_iterator_reset(itr2);
			while((par_arch_rec = list_next(itr2))) {
				if(assoc->parent_id
				   == par_arch_rec->assoc->id) {
					if(assoc->user)
						last_parent = par_arch_rec;
					else
						last_parent
							= last_slurmdb_parent
							= par_arch_rec;
					break;
				}
			}
		}

		if(!par_arch_rec) {
			list_append(arch_rec_list, arch_rec);
			last_parent = last_slurmdb_parent = arch_rec;
		} else
			list_append(par_arch_rec->childern, arch_rec);

		list_append(total_assoc_list, arch_rec);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

	list_destroy(total_assoc_list);
//	info("got %d", list_count(arch_rec_list));
	_sort_slurmdb_hierarchical_rec_list(arch_rec_list);

	return arch_rec_list;
}

/* IN/OUT: tree_list a list of slurmdb_print_tree_t's */
extern char *slurmdb_tree_name_get(char *name, char *parent, List tree_list)
{
	ListIterator itr = NULL;
	slurmdb_print_tree_t *slurmdb_print_tree = NULL;
	slurmdb_print_tree_t *par_slurmdb_print_tree = NULL;

	if(!tree_list)
		return NULL;

	itr = list_iterator_create(tree_list);
	while((slurmdb_print_tree = list_next(itr))) {
		/* we don't care about users in this list.  They are
		   only there so we don't leak memory */
		if(slurmdb_print_tree->user)
			continue;

		if(!strcmp(name, slurmdb_print_tree->name))
			break;
		else if(parent && !strcmp(parent, slurmdb_print_tree->name))
			par_slurmdb_print_tree = slurmdb_print_tree;

	}
	list_iterator_destroy(itr);

	if(parent && slurmdb_print_tree)
		return slurmdb_print_tree->print_name;

	slurmdb_print_tree = xmalloc(sizeof(slurmdb_print_tree_t));
	slurmdb_print_tree->name = xstrdup(name);
	if(par_slurmdb_print_tree)
		slurmdb_print_tree->spaces =
			xstrdup_printf(" %s", par_slurmdb_print_tree->spaces);
	else
		slurmdb_print_tree->spaces = xstrdup("");

	/* user account */
	if(name[0] == '|') {
		slurmdb_print_tree->print_name = xstrdup_printf(
			"%s%s", slurmdb_print_tree->spaces, parent);
		slurmdb_print_tree->user = 1;
	} else
		slurmdb_print_tree->print_name = xstrdup_printf(
			"%s%s", slurmdb_print_tree->spaces, name);

	list_append(tree_list, slurmdb_print_tree);

	return slurmdb_print_tree->print_name;
}

extern int set_qos_bitstr_from_list(bitstr_t *valid_qos, List qos_list)
{
	ListIterator itr = NULL;
	bitoff_t bit = 0;
	int rc = SLURM_SUCCESS;
	char *temp_char = NULL;
	void (*my_function) (bitstr_t *b, bitoff_t bit);

	xassert(valid_qos);

	if(!qos_list)
		return SLURM_ERROR;

	itr = list_iterator_create(qos_list);
	while((temp_char = list_next(itr))) {
		if(temp_char[0] == '-') {
			temp_char++;
			my_function = bit_clear;
		} else if(temp_char[0] == '+') {
			temp_char++;
			my_function = bit_set;
		} else
			my_function = bit_set;
		bit = atoi(temp_char);
		if(bit >= bit_size(valid_qos)) {
			rc = SLURM_ERROR;
			break;
		}
		(*(my_function))(valid_qos, bit);
	}
	list_iterator_destroy(itr);

	return rc;
}

extern char *get_qos_complete_str_bitstr(List qos_list, bitstr_t *valid_qos)
{
	List temp_list = NULL;
	char *temp_char = NULL;
	char *print_this = NULL;
	ListIterator itr = NULL;
	int i = 0;

	if(!qos_list || !list_count(qos_list)
	   || !valid_qos || (bit_ffs(valid_qos) == -1))
		return xstrdup("");

	temp_list = list_create(NULL);

	for(i=0; i<bit_size(valid_qos); i++) {
		if(!bit_test(valid_qos, i))
			continue;
		if((temp_char = slurmdb_qos_str(qos_list, i)))
			list_append(temp_list, temp_char);
	}
	list_sort(temp_list, (ListCmpF)slurm_sort_char_list_asc);
	itr = list_iterator_create(temp_list);
	while((temp_char = list_next(itr))) {
		if(print_this)
			xstrfmtcat(print_this, ",%s", temp_char);
		else
			print_this = xstrdup(temp_char);
	}
	list_iterator_destroy(itr);
	list_destroy(temp_list);

	if(!print_this)
		return xstrdup("");

	return print_this;
}

extern char *get_qos_complete_str(List qos_list, List num_qos_list)
{
	List temp_list = NULL;
	char *temp_char = NULL;
	char *print_this = NULL;
	ListIterator itr = NULL;
	int option = 0;

	if(!qos_list || !list_count(qos_list)
	   || !num_qos_list || !list_count(num_qos_list))
		return xstrdup("");

	temp_list = list_create(slurm_destroy_char);

	itr = list_iterator_create(num_qos_list);
	while((temp_char = list_next(itr))) {
		option = 0;
		if(temp_char[0] == '+' || temp_char[0] == '-') {
			option = temp_char[0];
			temp_char++;
		}
		temp_char = slurmdb_qos_str(qos_list, atoi(temp_char));
		if(temp_char) {
			if(option)
				list_append(temp_list, xstrdup_printf(
						    "%c%s", option, temp_char));
			else
				list_append(temp_list, xstrdup(temp_char));
		}
	}
	list_iterator_destroy(itr);
	list_sort(temp_list, (ListCmpF)slurm_sort_char_list_asc);
	itr = list_iterator_create(temp_list);
	while((temp_char = list_next(itr))) {
		if(print_this)
			xstrfmtcat(print_this, ",%s", temp_char);
		else
			print_this = xstrdup(temp_char);
	}
	list_iterator_destroy(itr);
	list_destroy(temp_list);

	if(!print_this)
		return xstrdup("");

	return print_this;
}

extern char *get_classification_str(uint16_t class)
{
	bool classified = class & SLURMDB_CLASSIFIED_FLAG;
	slurmdb_classification_type_t type = class & SLURMDB_CLASS_BASE;

	switch(type) {
	case SLURMDB_CLASS_NONE:
		return NULL;
		break;
	case SLURMDB_CLASS_CAPACITY:
		if(classified)
			return "*Capacity";
		else
			return "Capacity";
		break;
	case SLURMDB_CLASS_CAPABILITY:
		if(classified)
			return "*Capability";
		else
			return "Capability";
		break;
	case SLURMDB_CLASS_CAPAPACITY:
		if(classified)
			return "*Capapacity";
		else
			return "Capapacity";
		break;
	default:
		if(classified)
			return "*Unknown";
		else
			return "Unknown";
		break;
	}
}

extern uint16_t str_2_classification(char *class)
{
	uint16_t type = 0;
	if(!class)
		return type;

	if(slurm_strcasestr(class, "capac"))
		type = SLURMDB_CLASS_CAPACITY;
	else if(slurm_strcasestr(class, "capab"))
		type = SLURMDB_CLASS_CAPABILITY;
	else if(slurm_strcasestr(class, "capap"))
		type = SLURMDB_CLASS_CAPAPACITY;

	if(slurm_strcasestr(class, "*"))
		type |= SLURMDB_CLASSIFIED_FLAG;
	else if(slurm_strcasestr(class, "class"))
		type |= SLURMDB_CLASSIFIED_FLAG;

	return type;
}

extern char *slurmdb_problem_str_get(uint16_t problem)
{
	slurmdb_problem_type_t type = problem;

	switch(type) {
	case SLURMDB_PROBLEM_NOT_SET:
		return NULL;
		break;
	case SLURMDB_PROBLEM_ACCT_NO_ASSOC:
		return "Account has no Associations";
		break;
	case SLURMDB_PROBLEM_ACCT_NO_USERS:
		return "Account has no users";
		break;
	case SLURMDB_PROBLEM_USER_NO_ASSOC:
		return "User has no Associations";
		break;
	case SLURMDB_PROBLEM_USER_NO_UID:
		return "User does not have a uid";
		break;
	default:
		return "Unknown";
		break;
	}
}

extern uint16_t str_2_slurmdb_problem(char *problem)
{
	uint16_t type = 0;

	if(!problem)
		return type;

	if(slurm_strcasestr(problem, "account no associations"))
		type = SLURMDB_PROBLEM_USER_NO_ASSOC;
	else if(slurm_strcasestr(problem, "account no users"))
		type = SLURMDB_PROBLEM_ACCT_NO_USERS;
	else if(slurm_strcasestr(problem, "user no associations"))
		type = SLURMDB_PROBLEM_USER_NO_ASSOC;
	else if(slurm_strcasestr(problem, "user no uid"))
		type = SLURMDB_PROBLEM_USER_NO_UID;

	return type;
}

extern void log_assoc_rec(slurmdb_association_rec_t *assoc_ptr,
			  List qos_list)
{
	xassert(assoc_ptr);

	debug2("association rec id : %u", assoc_ptr->id);
	debug2("  acct             : %s", assoc_ptr->acct);
	debug2("  cluster          : %s", assoc_ptr->cluster);

	if(assoc_ptr->shares_raw == INFINITE)
		debug2("  RawShares        : NONE");
	else if(assoc_ptr->shares_raw != NO_VAL)
		debug2("  RawShares        : %u", assoc_ptr->shares_raw);

	if(assoc_ptr->grp_cpu_mins == INFINITE)
		debug2("  GrpCPUMins       : NONE");
	else if(assoc_ptr->grp_cpu_mins != NO_VAL)
		debug2("  GrpCPUMins       : %llu", assoc_ptr->grp_cpu_mins);

	if(assoc_ptr->grp_cpus == INFINITE)
		debug2("  GrpCPUs          : NONE");
	else if(assoc_ptr->grp_cpus != NO_VAL)
		debug2("  GrpCPUs          : %u", assoc_ptr->grp_cpus);

	if(assoc_ptr->grp_jobs == INFINITE)
		debug2("  GrpJobs          : NONE");
	else if(assoc_ptr->grp_jobs != NO_VAL)
		debug2("  GrpJobs          : %u", assoc_ptr->grp_jobs);

	if(assoc_ptr->grp_nodes == INFINITE)
		debug2("  GrpNodes         : NONE");
	else if(assoc_ptr->grp_nodes != NO_VAL)
		debug2("  GrpNodes         : %u", assoc_ptr->grp_nodes);

	if(assoc_ptr->grp_submit_jobs == INFINITE)
		debug2("  GrpSubmitJobs    : NONE");
	else if(assoc_ptr->grp_submit_jobs != NO_VAL)
		debug2("  GrpSubmitJobs    : %u", assoc_ptr->grp_submit_jobs);

	if(assoc_ptr->grp_wall == INFINITE)
		debug2("  GrpWall          : NONE");
	else if(assoc_ptr->grp_wall != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc_ptr->grp_wall,
			      time_buf, sizeof(time_buf));
		debug2("  GrpWall          : %s", time_buf);
	}

	if(assoc_ptr->max_cpu_mins_pj == INFINITE)
		debug2("  MaxCPUMins       : NONE");
	else if(assoc_ptr->max_cpu_mins_pj != NO_VAL)
		debug2("  MaxCPUMins       : %llu", assoc_ptr->max_cpu_mins_pj);

	if(assoc_ptr->max_cpus_pj == INFINITE)
		debug2("  MaxCPUs          : NONE");
	else if(assoc_ptr->max_cpus_pj != NO_VAL)
		debug2("  MaxCPUs          : %u", assoc_ptr->max_cpus_pj);

	if(assoc_ptr->max_jobs == INFINITE)
		debug2("  MaxJobs          : NONE");
	else if(assoc_ptr->max_jobs != NO_VAL)
		debug2("  MaxJobs          : %u", assoc_ptr->max_jobs);

	if(assoc_ptr->max_nodes_pj == INFINITE)
		debug2("  MaxNodes         : NONE");
	else if(assoc_ptr->max_nodes_pj != NO_VAL)
		debug2("  MaxNodes         : %u", assoc_ptr->max_nodes_pj);

	if(assoc_ptr->max_submit_jobs == INFINITE)
		debug2("  MaxSubmitJobs    : NONE");
	else if(assoc_ptr->max_submit_jobs != NO_VAL)
		debug2("  MaxSubmitJobs    : %u", assoc_ptr->max_submit_jobs);

	if(assoc_ptr->max_wall_pj == INFINITE)
		debug2("  MaxWall          : NONE");
	else if(assoc_ptr->max_wall_pj != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc_ptr->max_wall_pj,
			      time_buf, sizeof(time_buf));
		debug2("  MaxWall          : %s", time_buf);
	}

	if(assoc_ptr->qos_list) {
		char *temp_char = get_qos_complete_str(qos_list,
						       assoc_ptr->qos_list);
		if(temp_char) {
			debug2("  Qos              : %s", temp_char);
			xfree(temp_char);
		}
	} else {
		debug2("  Qos              : %s", "Normal");
	}

	if(assoc_ptr->parent_acct)
		debug2("  ParentAccount    : %s", assoc_ptr->parent_acct);
	if(assoc_ptr->partition)
		debug2("  Partition        : %s", assoc_ptr->partition);
	if(assoc_ptr->user)
		debug2("  User             : %s(%u)",
		       assoc_ptr->user, assoc_ptr->uid);

	if(assoc_ptr->usage) {
		if(assoc_ptr->usage->shares_norm != (double)NO_VAL)
			debug2("  NormalizedShares : %f",
			       assoc_ptr->usage->shares_norm);

		if(assoc_ptr->usage->level_shares != NO_VAL)
			debug2("  LevelShares      : %u",
			       assoc_ptr->usage->level_shares);


		debug2("  UsedJobs         : %u", assoc_ptr->usage->used_jobs);
		debug2("  RawUsage         : %Lf", assoc_ptr->usage->usage_raw);
	}
}
