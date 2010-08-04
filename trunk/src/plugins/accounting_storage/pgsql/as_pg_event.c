/*****************************************************************************\
 *  as_pg_event.c - accounting interface to pgsql - cluster/node event related
 *  functions.
 *
 *  $Id: as_pg_event.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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
#include "as_pg_common.h"

char *event_table = "cluster_event_table";
static storage_field_t event_table_fields[] = {
	{ "node_name", "TEXT DEFAULT '' NOT NULL" },
	{ "cluster", "TEXT NOT NULL" },
	{ "cpu_count", "INTEGER NOT NULL" },
	{ "state", "INTEGER DEFAULT 0 NOT NULL" },
	{ "period_start", "INTEGER NOT NULL" },
	{ "period_end", "INTEGER DEFAULT 0 NOT NULL" },
	{ "reason", "TEXT NOT NULL" },
	{ "reason_uid", "INTEGER DEFAULT -2 NOT NULL" },
	{ "cluster_nodes", "TEXT NOT NULL DEFAULT ''" },
	{ NULL, NULL}
};
static char *event_table_constraint = ", "
	"PRIMARY KEY (node_name, cluster, period_start) "
	")";


/*
 * _create_function_record_node_down - create a PL/PGSQL function to record
 *   node down event
 *
 * IN db_conn: database connection
 * RET: error code
 */
static int
_create_function_record_node_down(PGconn *db_conn)
{
	char *create_line = xstrdup_printf(
		"CREATE OR REPLACE FUNCTION record_node_down "
		"(cl TEXT, nn TEXT, st INTEGER, rs TEXT, rs_uid INTEGER, "
		" cpu INTEGER, tm INTEGER) RETURNS VOID AS $$"
		"BEGIN "
		"  UPDATE %s SET period_end=(tm-1) WHERE cluster=cl "
		"    AND period_end=0 AND node_name=nn;"
		"  LOOP"
		"    BEGIN "
		"      INSERT INTO %s (node_name, cluster, cpu_count, "
		"          period_start, state, reason, reason_uid) "
		"        VALUES (nn, cl, cpu, tm, st, rs, rs_uid);"
		"      RETURN;"
		"    EXCEPTION WHEN UNIQUE_VIOLATION THEN "
		"      UPDATE %s SET period_end=0"
		"        WHERE cluster=cl AND node_name=nn AND period_start=tm;"
		"      IF FOUND THEN RETURN; END IF;"
		"    END; "
		"  END LOOP; "
		"END; $$ LANGUAGE PLPGSQL;",
		event_table, event_table, event_table);
	return create_function_xfree(db_conn, create_line);
}

/*
 * check_clusteracct_tables - check clusteracct related tables and functions
 * IN pg_conn: database connection
 * IN user: database owner
 * RET: error code
 */
extern int
check_clusteracct_tables(PGconn *db_conn, char *user)
{
	int rc;

	rc = check_table(db_conn, event_table, event_table_fields,
			 event_table_constraint, user);
	rc |= _create_function_record_node_down(db_conn);
	return rc;
}

/*
 * get_cluster_cpu_nodes - fill in cluster cpu and node count
 *
 * IN pg_conn: database connection
 * IN/OUT cluster: cluster record
 * RET: error code
 */
extern int
get_cluster_cpu_nodes(pgsql_conn_t *pg_conn, slurmdb_cluster_rec_t *cluster)
{
	PGresult *result;
	char *query = xstrdup_printf(
		"SELECT cpu_count, cluster_nodes FROM %s "
		"WHERE cluster='%s' AND period_end=0 "
		"AND node_name='' LIMIT 1",
		event_table, cluster->name);
	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	if (PQntuples(result)) {
		char *tmp = PG_VAL(1);
		cluster->cpu_count = atoi(PG_VAL(0));
		if (tmp && tmp[0])
			cluster->nodes = xstrdup(tmp);
	}
	PQclear(result);
	return SLURM_SUCCESS;
}

/*
 * cs_pg_node_down - load into storage the event of node down
 * IN pg_conn: database connection
 * IN cluster: cluster of the down node
 * RET: error code
 */
extern int
cs_pg_node_down(pgsql_conn_t *pg_conn,
		struct node_record *node_ptr,
		time_t event_time, char *reason, uint32_t reason_uid)
{
	uint16_t cpus;
	int rc = SLURM_ERROR;
	char *query = NULL, *my_reason;

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	if (!node_ptr) {
		error("as/pg: cs_pg_node_down: No node_ptr give!");
		return SLURM_ERROR;
	}

	if (slurmctld_conf.fast_schedule && !slurmdbd_conf)
		cpus = node_ptr->config_ptr->cpus;
	else
		cpus = node_ptr->cpus;

	if (reason)
		my_reason = reason;
	else
		my_reason = node_ptr->reason;

	debug2("inserting %s(%s) with %u cpus",
	       node_ptr->name, pg_conn->cluster_name, cpus);

	query = xstrdup_printf(
		"SELECT record_node_down('%s', '%s', %d, '%s', %d, %d, %d);",
		pg_conn->cluster_name, node_ptr->name,
		(int)node_ptr->node_state, my_reason, (int)reason_uid,
		(int)cpus, (int)event_time);

	rc = DEF_QUERY_RET_RC;
	return rc;
}

/*
 * cs_pg_node_up - load into storage the event of node up
 * IN pg_conn: database connection
 * IN cluster: cluster of the down node
 * RET: error code
 */
extern int
cs_pg_node_up(pgsql_conn_t *pg_conn,
	      struct node_record *node_ptr, time_t event_time)
{
	char* query;
	int rc = SLURM_ERROR;

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	query = xstrdup_printf(
		"UPDATE %s SET period_end=%d WHERE cluster='%s' "
		"AND period_end=0 AND node_name='%s'",
		event_table, (event_time-1), pg_conn->cluster_name,
		node_ptr->name);
	rc = DEF_QUERY_RET_RC;
	return rc;
}

/*
 * cs_pg_register_ctld - cluster registration
 *   SHOULD NOT be called from slurmdbd, where modify_clusters
 *   will be called on cluster registration
 * IN pg_conn: database connection
 * IN cluster: cluster name
 * IN port: tcp port slurmctld listening on
 * RET: error code
 */
extern int
cs_pg_register_ctld(pgsql_conn_t *pg_conn, char *cluster, uint16_t port)
{
	char *query = NULL, *address = NULL;
	char hostname[255];
	int rc;
	time_t now = time(NULL);

	if (slurmdbd_conf)
		fatal("clusteracct_storage_g_register_ctld "
		      "should never be called from the slurmdbd.");

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	info("Registering slurmctld for cluster %s at port %u in database.",
	     cluster, port);
	gethostname(hostname, sizeof(hostname));

	/* check if we are running on the backup controller */
	if (slurmctld_conf.backup_controller
	   && !strcmp(slurmctld_conf.backup_controller, hostname)) {
		address = slurmctld_conf.backup_addr;
	} else
		address = slurmctld_conf.control_addr;

	query = xstrdup_printf(
		"UPDATE %s SET deleted=0, mod_time=%d, "
		"control_host='%s', control_port=%u, rpc_version=%d "
		"WHERE name='%s';",
		cluster_table, now, address, port,
		SLURMDBD_VERSION, cluster);
	xstrfmtcat(query, "INSERT INTO %s "
		   "(timestamp, action, name, actor, info) "
		   "VALUES (%d, %d, '%s', '%s', '%s %u');",
		   txn_table, now, DBD_MODIFY_CLUSTERS, cluster,
		   slurmctld_conf.slurm_user_name, address, port);
	rc = DEF_QUERY_RET_RC;
	return rc;
}

/*
 * cs_pg_cluster_cpus - cluster processor count change
 *
 * IN pg_conn: database connection
 * IN cluster: cluster name
 * IN cluster_nodes: nodes in cluster
 * IN cpus: processor count
 * IN event_time: event time
 * RET: error code
 */
extern int
cs_pg_cluster_cpus(pgsql_conn_t *pg_conn, char *cluster_nodes,
		   uint32_t cpus, time_t event_time)
{
	PGresult *result = NULL;
	char* query;
	int rc = SLURM_SUCCESS, got_cpus = 0, first = 0;

	if (check_db_connection(pg_conn) != SLURM_SUCCESS)
		return ESLURM_DB_CONNECTION;

	/* Record the processor count */
	query = xstrdup_printf(
		"SELECT cpu_count, cluster_nodes FROM %s WHERE cluster='%s' "
		"AND period_end=0 AND node_name='' LIMIT 1;",
		event_table, pg_conn->cluster_name);
	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	/* we only are checking the first one here */
	if (!PQntuples(result)) {
		debug("We don't have an entry for this machine %s "
		      "most likely a first time running.",
		      pg_conn->cluster_name);
		/* Get all nodes in a down state and jobs pending or running.
		 * This is for the first time a cluster registers
		 *
		 * We will return ACCOUNTING_FIRST_REG so this
		 * is taken care of since the message thread
		 * may not be up when we run this in the controller or
		 * in the slurmdbd.
		 */
		first = 1;
		goto add_it;
	}
	got_cpus = atoi(PG_VAL(0));
	if (got_cpus == cpus) {
		debug3("we have the same cpu count as before for %s, "
		       "no need to update the database.",
		       pg_conn->cluster_name);
		if (cluster_nodes) {
			if (PG_EMPTY(1)) {
				debug("Adding cluster nodes '%s' to "
				      "last instance of cluster '%s'.",
				      cluster_nodes, pg_conn->cluster_name);
				query = xstrdup_printf(
					"UPDATE %s SET cluster_nodes='%s' "
					"WHERE cluster='%s' "
					"AND period_end=0 AND node_name='';",
					event_table, cluster_nodes,
					pg_conn->cluster_name);
				rc = DEF_QUERY_RET_RC;
				goto end_it;
			} else if (!strcmp(cluster_nodes,
					  PG_VAL(1))) {
				debug3("we have the same nodes in the cluster "
				       "as before no need to "
				       "update the database.");
				goto end_it;
			}
		} else
			goto end_it;
	} else
		debug("%s has changed from %d cpus to %u",
		      pg_conn->cluster_name, got_cpus, cpus);

	/* reset all the entries for this cluster since the cpus
	   changed some of the downed nodes may have gone away.
	   Request them again with ACCOUNTING_FIRST_REG */
	query = xstrdup_printf("UPDATE %s SET period_end=%u "
			       "WHERE cluster='%s' AND period_end=0;",
			       event_table, (event_time-1),
			       pg_conn->cluster_name);
	rc = DEF_QUERY_RET_RC;
	first = 1;
	if (rc != SLURM_SUCCESS)
		goto end_it;
add_it:
	query = xstrdup_printf(
		"INSERT INTO %s (cluster, cpu_count, period_start, reason) "
		"VALUES ('%s', %u, %d, 'Cluster processor count')",
		event_table, pg_conn->cluster_name, cpus, event_time);
	rc = DEF_QUERY_RET_RC;

end_it:
	if (first && rc == SLURM_SUCCESS)
		rc = ACCOUNTING_FIRST_REG;

	return rc;
}

/*
 * cs_pg_get_usage - get cluster usage data
 *
 * IN pg_conn: database connection
 * IN/OUT cluster_rec: usage of which cluster to get
 * IN type: DBD_GET_CLUSTER_USAGE
 * IN start: start time
 * IN end: end time
 * RET: error code
 */
extern int
cs_pg_get_usage(pgsql_conn_t *pg_conn, uid_t uid,
		slurmdb_cluster_rec_t *cluster_rec,
		int type, time_t start, time_t end)
{
	PGresult *result = NULL;
	char *query = NULL, *usage_table = NULL;
	char *cu_fields = "alloc_cpu_secs,down_cpu_secs,pdown_cpu_secs,"
		"idle_cpu_secs,resv_cpu_secs,over_cpu_secs,cpu_count,"
		"period_start";
	enum {
		CU_ACPU,
		CU_DCPU,
		CU_PDCPU,
		CU_ICPU,
		CU_RCPU,
		CU_OCPU,
		CU_CPU_COUNT,
		CU_START,
		CU_COUNT
	};

	if (!cluster_rec->name) {
		error("We need a cluster name to set data for");
		return SLURM_ERROR;
	}

	usage_table = cluster_day_table;
	if (set_usage_information(&usage_table, type, &start, &end)
	   != SLURM_SUCCESS)
		return SLURM_ERROR;

	query = xstrdup_printf(
		"SELECT %s FROM %s WHERE (period_start < %d "
		"AND period_start >= %d) AND cluster='%s'",
		cu_fields, usage_table, end, start, cluster_rec->name);
	result = DEF_QUERY_RET;
	if (!result)
		return SLURM_ERROR;

	if (!cluster_rec->accounting_list)
		cluster_rec->accounting_list =
			list_create(slurmdb_destroy_cluster_accounting_rec);
	FOR_EACH_ROW {
		slurmdb_cluster_accounting_rec_t *accounting_rec =
			xmalloc(sizeof(slurmdb_cluster_accounting_rec_t));

		accounting_rec->alloc_secs = atoll(ROW(CU_ACPU));
		accounting_rec->down_secs = atoll(ROW(CU_DCPU));
		accounting_rec->pdown_secs = atoll(ROW(CU_PDCPU));
		accounting_rec->idle_secs = atoll(ROW(CU_ICPU));
		accounting_rec->over_secs = atoll(ROW(CU_OCPU));
		accounting_rec->resv_secs = atoll(ROW(CU_RCPU));
		accounting_rec->cpu_count = atoi(ROW(CU_CPU_COUNT));
		accounting_rec->period_start = atoi(ROW(CU_START));
		list_append(cluster_rec->accounting_list, accounting_rec);
	} END_EACH_ROW;
	PQclear(result);
	return SLURM_SUCCESS;
}

/*
 * as_pg_get_events - get cluster events
 *
 * IN pg_conn: database connection
 * IN uid: user performing get operation
 * event_cond: filter condition
 * RET: list of events
 */
extern List
as_pg_get_events(pgsql_conn_t *pg_conn, uid_t uid, 
		 slurmdb_event_cond_t *event_cond)
{
        char *query = NULL;
        char *cond = NULL;
        List ret_list = NULL;
        PGresult *result = NULL;
        time_t now = time(NULL);

        /* if this changes you will need to edit the corresponding enum */
	char *ge_fields = "cluster,cluster_nodes,cpu_count,node_name,state,"
		"time_start,time_end,reason,reason_uid";
        enum {
		GE_CLUSTER,
                GE_CNODES,
                GE_CPU,
                GE_NODE,
                GE_STATE,
                GE_START,
                GE_END,
                GE_REASON,
                GE_REASON_UID,
                GE_COUNT
        };

        if (check_db_connection(pg_conn) != SLURM_SUCCESS)
                return NULL;

	cond = xstrdup("WHERE TRUE");
	
        if (!event_cond)
                goto empty;

        if (event_cond->cpus_min) {
                if (event_cond->cpus_max) {
                        xstrfmtcat(cond, " AND (cpu_count BETWEEN %u AND %u)",
                                   event_cond->cpus_min, event_cond->cpus_max);

                } else {
                        xstrfmtcat(cond, " AND (cpu_count='%u')",
                                   event_cond->cpus_min);
                }
        }

        switch(event_cond->event_type) {
        case SLURMDB_EVENT_ALL:
                break;
        case SLURMDB_EVENT_CLUSTER:
                xstrcat(cond, " AND (node_name = '')");
                break;
        case SLURMDB_EVENT_NODE:
                xstrcat(cond, " AND (node_name != '')");
                break;
        default:
                error("Unknown event %u doing all", event_cond->event_type);
                break;
        }

	concat_cond_list(event_cond->node_list, NULL, "node_name", &cond);

        if (event_cond->period_start) {
                if (!event_cond->period_end)
                        event_cond->period_end = now;
                xstrfmtcat(cond,
                           " AND (time_start < %d) "
                           " AND (time_end >= %d OR time_end = 0)",
                           event_cond->period_end, event_cond->period_start);
        }

	concat_like_cond_list(event_cond->reason_list, NULL, "reason", &cond);
	concat_cond_list(event_cond->reason_uid_list, NULL, "reason_uid", &cond);
	concat_cond_list(event_cond->state_list, NULL, "state", &cond);
	concat_cond_list(event_cond->cluster_list, NULL, "cluster", &cond);

empty:
        query = xstrdup_printf("SELECT %s from %s %s ORDER BY cluster,time_start;",
                               ge_fields, event_table, cond);
	xfree(cond);
	result = DEF_QUERY_RET;
	if (!result) {
		return NULL;
	}

        ret_list = list_create(slurmdb_destroy_event_rec);
	FOR_EACH_ROW {
                slurmdb_event_rec_t *event =
                        xmalloc(sizeof(slurmdb_event_rec_t));
                list_append(ret_list, event);

                event->cluster = xstrdup(ROW(GE_CLUSTER));

                if (ISEMPTY(GE_NODE)) {
                        event->event_type = SLURMDB_EVENT_CLUSTER;
                } else
                        event->node_name = xstrdup(ROW(GE_NODE));
                        event->event_type = SLURMDB_EVENT_NODE;

                event->cpu_count = atoi(ROW(GE_CPU));
                event->state = atoi(ROW(GE_STATE));
                event->period_start = atoi(ROW(GE_START));
                event->period_end = atoi(ROW(GE_END));

                if (!ISEMPTY(GE_REASON))
                        event->reason = xstrdup(ROW(GE_REASON));
                event->reason_uid = atoi(ROW(GE_REASON_UID));

                if (!ISEMPTY(GE_CNODES))
                        event->cluster_nodes =
                                xstrdup(ROW(GE_CNODES));
        } END_EACH_ROW;
        PQclear(result);

        return ret_list;
}

