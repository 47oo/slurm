/* 
 * slurmlib.h - descriptions of slurm APIs
 * see slurm.h for documentation on external functions and data structures
 *
 * author: moe jette, jette@llnl.gov
 */

#define BUILD_SIZE	128
#define FEATURE_SIZE	1024
#define MAX_ID_LEN	32
#define MAX_NAME_LEN	16
#define NODE_STRUCT_VERSION 1
#define SLURMCTLD_HOST	"134.9.55.42"
#define SLURMCTLD_PORT	1543

/* INFINITE is used to identify unlimited configurations,  */
/* eg. the maximum count of nodes any job may use in some partition */
#define	INFINITE (0xffffffff)

/* last entry must be STATE_END, keep in sync with node_state_string    	*/
/* any value less than or equal to zero is down. if a node was in state 	*/
/* STATE_BUSY and stops responding, its state becomes -(STATE_BUSY), etc.	*/
enum node_states {
	STATE_DOWN,		/* node is not responding */
	STATE_UNKNOWN,		/* node's initial state, unknown */
	STATE_IDLE,		/* node idle and available for use */
	STATE_STAGE_IN,		/* node has been allocated, job not yet running */
	STATE_BUSY,		/* node has been allocated, job currently */
	STATE_DRAINED,		/* node idle and not to be allocated future work */
	STATE_DRAINING,		/* node in use, but not to be allocated future work */
	STATE_END		/* last entry in table */
};

struct node_table {
	char *name;		/* name of the node. a null name indicates defunct node */
	uint32_t node_state;	/* state of the node, see node_states */
	uint32_t cpus;		/* count of processors running on the node */
	uint32_t real_memory;	/* megabytes of real memory on the node */
	uint32_t tmp_disk;	/* megabytes of total disk in TMP_FS */
	uint32_t weight;	/* desirability of use */
	char *features;		/* comma delimited feature list */
	char *partition;	/* partition name */ 
};

struct node_buffer {
	time_t last_update;	/* time of last buffer update */
	int node_count;		/* count of entries in node_table */
	void *raw_buffer_ptr;	/* raw network buffer info */
	struct node_table *node_table_ptr;
};

/*
 * slurm_allocate - allocate nodes for a job with supplied contraints. 
 * input: spec - specification of the job's constraints
 *        job_id - place into which a job_id pointer can be placed
 * output: job_id - node_list - list of allocated nodes
 *         returns 0 if no error, EINVAL if the request is invalid, 
 *			EAGAIN if the request can not be satisfied at present
 * NOTE: required specifications include: User=<uid> Script=<pathname>
 *	optional specifications include: Contiguous=<YES|NO> 
 *	Distribution=<BLOCK|CYCLE> Features=<features> Groups=<groups>
 *	JobId=<id> JobName=<name> Key=<credential> MinProcs=<count>
 *	MinRealMemory=<MB> MinTmpDisk=<MB> Partition=<part_name>
 *	Priority=<integer> ProcsPerTask=<count> ReqNodes=<node_list>
 *	Shared=<YES|NO> TimeLimit=<minutes> TotalNodes=<count>
 *	TotalProcs=<count>
 * NOTE: the calling function must free the allocated storage at node_list[0]
 */
extern int slurm_allocate (char *spec, char **node_list, char **job_id);

/*
 * slurm_cancel - cancel the specified job 
 * input: job_id - the job_id to be cancelled
 * output: returns 0 if no error, EINVAL if the request is invalid, 
 *			EAGAIN if the request can not be satisfied at present
 */
extern int slurm_cancel (char *job_id);

/*
 * slurm_free_build_info - free the build information buffer (if allocated).
 * NOTE: buffer is loaded by slurm_load_build and used by slurm_load_build_name.
 */
extern void slurm_free_build_info (void);

/*
 * slurm_free_node_info - free the node information buffer (if allocated)
 * NOTE: buffer is loaded by slurm_load_node.
 */
extern void slurm_free_node_info (struct node_buffer *node_buffer_ptr);

/*
 * free_job_info - free the job information buffer (if allocated)
 * NOTE: buffer is loaded by load_job and used by load_job_name.
 */
extern void free_job_info (void);

/*
 * free_part_info - free the partition information buffer (if allocated)
 * NOTE: buffer is loaded by load_part and used by load_part_name.
 */
extern void free_part_info (void);

/*
 * slurm_load_build - update the build information buffer for use by info gathering APIs
 * output: returns 0 if no error, einval if the buffer is invalid, enomem if malloc failure.
 * NOTE: buffer is used by slurm_load_build_name and freed by slurm_free_build_info.
 */
extern int slurm_load_build ();

/* 
 * slurm_load_build_name - load the state information about the named build parameter
 * input: req_name - name of the parameter for which information is requested
 *		     if "", then get info for the first parameter in list
 *        next_name - location into which the name of the next parameter is 
 *                   stored, "" if no more
 *        value - pointer to location into which the information is to be stored
 * output: req_name - the parameter's name is stored here
 *         next_name - the name of the next parameter in the list is stored here
 *         value - the parameter's state information
 *         returns 0 on success, enoent if not found, or einval if buffer is bad
 * NOTE:  req_name, next_name, and value must be declared by caller with have 
 *        length BUILD_SIZE or larger
 * NOTE: buffer is loaded by slurm_load_build and freed by slurm_free_build_info.
 */
extern int slurm_load_build_name (char *req_name, char *next_name, char *value);

/*
 * slurm_load_node - load the supplied node information buffer for use by info 
 *	gathering APIs if node records have changed since the time specified. 
 * input: update_time - time of last update
 *	node_buffer_ptr - place to park node_buffer pointer
 * output: node_buffer_ptr - pointer to allocated node_buffer
 *	returns -1 if no update since update_time, 
 *		0 if update with no error, 
 *		EINVAL if the buffer (version or otherwise) is invalid, 
 *		ENOMEM if malloc failure
 * NOTE: the allocated memory at node_buffer_ptr freed by slurm_free_node_info.
 */
extern int slurm_load_node (time_t update_time, struct node_buffer **node_buffer_ptr);

/*
 * slurm_submit - submit/queue a job with supplied contraints. 
 * input: spec - specification of the job's constraints
 *	job_id - place to store id of submitted job
 * output: job_id - the job's id
 *	returns 0 if no error, EINVAL if the request is invalid
 * NOTE: required specification include: Script=<script_path_name>
 *	User=<uid>
 * NOTE: optional specifications include: Contiguous=<YES|NO> 
 *	Distribution=<BLOCK|CYCLE> Features=<features> Groups=<groups>
 *	JobId=<id> JobName=<name> Key=<key> MinProcs=<count> 
 *	MinRealMemory=<MB> MinTmpDisk=<MB> Partition=<part_name>
 *	Priority=<integer> ProcsPerTask=<count> ReqNodes=<node_list>
 *	Shared=<YES|NO> TimeLimit=<minutes> TotalNodes=<count>
 *	TotalProcs=<count>
 */
extern int slurm_submit (char *spec, char **job_id);

/*
 * load_job - load the supplied job information buffer for use by info gathering 
 *	APIs if job records have changed since the time specified. 
 * input: buffer - pointer to job information buffer
 *        buffer_size - size of buffer
 * output: returns 0 if no error, EINVAL if the buffer is invalid,
 *		 ENOMEM if malloc failure
 * NOTE: buffer is used by load_job_config and freed by free_job_info.
 */
extern int load_job (time_t * last_update_time);

/*
 * load_part - update the partition information buffer for use by info gathering APIs if 
 *	partition records have changed since the time specified. 
 * input: last_update_time - pointer to time of last buffer
 * output: last_update_time - time reset if buffer is updated
 *         returns 0 if no error, einval if the buffer is invalid, enomem if malloc failure
 * NOTE: buffer is used by load_part_name and free by free_part_info.
 */
extern int load_part (time_t * last_update_time);

/* 
 * load_part_name - load the state information about the named partition
 * input: req_name - name of the partition for which information is requested
 *		     if "", then get info for the first partition in list
 *        next_name - location into which the name of the next partition is 
 *                   stored, "" if no more
 *        max_time, etc. - pointers into which the information is to be stored
 * output: req_name - the partition's name is stored here
 *         next_name - the name of the next partition in the list is stored here
 *         max_time, etc. - the partition's state information
 *         returns 0 on success, enoent if not found, or einval if buffer is bad
 * NOTE:  req_name and next_name must be declared by caller with have length MAX_NAME_LEN or larger.
 *        nodes and allow_groups must be declared by caller with length of FEATURE_SIZE or larger.
 * NOTE: buffer is loaded by load_part and free by free_part_info.
 */
extern int load_part_name (char *req_name, char *next_name, int *max_time,
		    int *max_nodes, int *total_nodes, int *total_cpus,
		    int *key, int *state_up, int *shared, int *default_flag,
		    char *nodes, char *allow_groups);

/* 
 * parse_node_name - parse the node name for regular expressions and return a sprintf format 
 * generate multiple node names as needed.
 * input: node_name - node name to parse
 * output: format - sprintf format for generating names
 *         start_inx - first index to used
 *         end_inx - last index value to use
 *         count_inx - number of index values to use (will be zero if none)
 *         return 0 if no error, error code otherwise
 * NOTE: the calling program must execute free(format) when the storage location is no longer needed
 */
extern int parse_node_name (char *node_name, char **format, int *start_inx,
			    int *end_inx, int *count_inx);

/* 
 * reconfigure - _ request that slurmctld re-read the configuration files
 * output: returns 0 on success, errno otherwise
 */
extern int reconfigure ();

/* 
 * update_config - _ request that slurmctld update its configuration per request
 * input: a line containing configuration information per the configuration file format
 * output: returns 0 on success, errno otherwise
 */
extern int update_config (char *spec);
