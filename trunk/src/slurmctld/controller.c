/* 
 * controller.c - main control machine daemon for slurm
 * see slurm.h for documentation on external functions and data structures
 *
 * NOTE: DEBUG_MODULE of read_config requires that it be loaded with 
 *       bits_bytes, partition_mgr, read_config, and node_mgr
 *
 * author: moe jette, jette@llnl.gov
 */

#ifdef have_config_h
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "slurm.h"
#include "slurmlib.h"

#define BUF_SIZE 1024
#define NO_VAL (-99)

int msg_from_root (void);
void slurmctld_req (int sockfd);

main (int argc, char *argv[]) {
	int error_code;
	int child_pid, cli_len, newsockfd, sockfd;
	struct sockaddr_in cli_addr, serv_addr;
	char node_name[MAX_NAME_LEN];

	error_code = init_slurm_conf ();
	if (error_code) {
#if DEBUG_SYSTEM
		fprintf (stderr, "slurmctld: init_slurm_conf error %d\n",
			 error_code);
#else
		syslog (LOG_ALERT, "slurmctld: init_slurm_conf error %d\n",
			error_code);
#endif
		abort ();
	}			

	error_code = read_slurm_conf (SLURM_CONF);
	if (error_code) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "slurmctld: error %d from read_slurm_conf reading %s\n",
			 error_code, SLURM_CONF);
#else
		syslog (LOG_ALERT,
			"slurmctld: error %d from read_slurm_conf reading %s\n",
			error_code, SLURM_CONF);
#endif
		abort ();
	}			

	error_code = gethostname (node_name, MAX_NAME_LEN);
	if (error_code != 0) {
#if DEBUG_SYSTEM
		fprintf (stderr, "slurmctld: error %d from gethostname\n",
			 error_code);
#else
		syslog (LOG_ALERT, "slurmctld: error %d from gethostname\n",
			error_code);
#endif
		abort ();
	}			
	if (strcmp (node_name, control_machine) != 0) {
#if DEBUG_SYSTEM
		fprintf (stderr,
			 "slurmctld: this machine (%s) is not the primary control machine (%s)\n",
			 node_name, control_machine);
#else
		syslog (LOG_ERR,
			"slurmctld: this machine (%s) is not the primary control machine (%s)\n",
			node_name, control_machine);
#endif
		exit (1);
	}			

	if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
#if DEBUG_SYSTEM
		fprintf (stderr, "slurmctld: error %d from socket\n", errno);
#else
		syslog (LOG_ALERT, "slurmctld: error %d from socket\n",
			errno);
#endif
		abort ();
	}			
	memset (&serv_addr, 0, sizeof (serv_addr));
	serv_addr.sin_family = PF_INET;
	serv_addr.sin_addr.s_addr = htonl (INADDR_ANY);
	serv_addr.sin_port = htons (SLURMCTLD_PORT);
	if (bind (sockfd, (struct sockaddr *) &serv_addr, sizeof (serv_addr))
	    < 0) {
#if DEBUG_SYSTEM
		fprintf (stderr, "slurmctld: error %d from bind\n", errno);
#else
		syslog (LOG_ALERT, "slurmctld: error %d from bind\n", errno);
#endif
		abort ();
	}			
	listen (sockfd, 5);
	while (1) {
		cli_len = sizeof (cli_addr);
		if ((newsockfd =
		     accept (sockfd, (struct sockaddr *) &cli_addr,
			     &cli_len)) < 0) {
#if DEBUG_SYSTEM
			fprintf (stderr, "slurmctld: error %d from accept\n",
				 errno);
#else
			syslog (LOG_ALERT,
				"slurmctld: error %d from accept\n", errno);
#endif
			abort ();
		}		

/* convert to pthread, tbd */
		slurmctld_req (newsockfd);	/* process the request */
		close (newsockfd);	/* close the new socket */

	}			
}				/* main */

/* 
 * dump_build - dump all build parameters to a buffer
 * input: buffer_ptr - location into which a pointer to the data is to be stored.
 *                     the data buffer is actually allocated by dump_part and the 
 *                     calling function must free the storage.
 *         buffer_size - location into which the size of the created buffer is in bytes
 * output: buffer_ptr - the pointer is set to the allocated buffer.
 *         buffer_size - set to size of the buffer in bytes
 *         returns 0 if no error, errno otherwise
 * NOTE: the buffer at *buffer_ptr must be freed by the caller
 * NOTE: if you make any changes here be sure to increment the value of BUILD_STRUCT_VERSION
 *       and make the corresponding changes to load_build_name in api/build_info.c
 */
int
dump_build (char **buffer_ptr, int *buffer_size)
{
	char *buffer;
	int buffer_offset, buffer_allocated, i, record_size;
	char out_line[BUILD_SIZE * 2];

	buffer_ptr[0] = NULL;
	*buffer_size = 0;
	buffer = NULL;
	buffer_offset = 0;
	buffer_allocated = 0;

	/* write haeader, version and time */
	sprintf (out_line, HEAD_FORMAT, (unsigned long) time (NULL),
		 BUILD_STRUCT_VERSION);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	/* write paramter records */
	sprintf (out_line, BUILD_STRUCT2_FORMAT, "BACKUP_INTERVAL",
		 BACKUP_INTERVAL);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "BACKUP_LOCATION",
		 BACKUP_LOCATION);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "CONTROL_DAEMON",
		 CONTROL_DAEMON);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT2_FORMAT, "CONTROLLER_TIMEOUT",
		 CONTROLLER_TIMEOUT);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "EPILOG", EPILOG);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT2_FORMAT, "HASH_BASE", HASH_BASE);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT2_FORMAT, "HEARTBEAT_INTERVAL",
		 HEARTBEAT_INTERVAL);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "INIT_PROGRAM", INIT_PROGRAM);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT2_FORMAT, "KILL_WAIT", KILL_WAIT);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "PRIORITIZE", PRIORITIZE);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "PROLOG", PROLOG);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "SERVER_DAEMON",
		 SERVER_DAEMON);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT2_FORMAT, "SERVER_TIMEOUT",
		 SERVER_TIMEOUT);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "SLURM_CONF", SLURM_CONF);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	sprintf (out_line, BUILD_STRUCT_FORMAT, "TMP_FS", TMP_FS);
	if (write_buffer
	    (&buffer, &buffer_offset, &buffer_allocated, out_line))
		goto cleanup;

	buffer = realloc (buffer, buffer_offset);
	if (buffer == NULL) {
#if DEBUG_SYSTEM
		fprintf (stderr, "dump_build: unable to allocate memory\n");
#else
		syslog (LOG_ALERT, "dump_build: unable to allocate memory\n");
#endif
		abort ();
	}			

	buffer_ptr[0] = buffer;
	*buffer_size = buffer_offset;
	return 0;

      cleanup:
	if (buffer)
		free (buffer);
	return EINVAL;
}


/*
 * slurmctld_req - process a slurmctld request from the given socket
 * input: sockfd - the socket with a request to be processed
 */
void
slurmctld_req (int sockfd) {
	int error_code, in_size, i;
	char in_line[BUF_SIZE], node_name[MAX_NAME_LEN];
	int cpus, real_memory, tmp_disk;
	char *node_name_ptr, *part_name, *time_stamp;
	time_t last_update;
	clock_t start_time;
	char *dump;
	int dump_size, dump_loc;

	in_size = recv (sockfd, in_line, sizeof (in_line), 0);

	/* Allocate:  allocate resources for a job */
	if (strncmp ("Allocate", in_line, 8) == 0) {
		start_time = clock ();
		node_name_ptr = NULL;
		error_code = select_nodes (&in_line[8], &node_name_ptr);  /* skip "Allocate" */
#if DEBUG_SYSTEM
		if (error_code)
			fprintf (stderr,
				 "slurmctld_req: error %d allocating resources for %s, ",
				 error_code, &in_line[8]);
		else
			fprintf (stderr,
				 "slurmctld_req: allocated nodes %s to job %s, ",
				 node_name_ptr, &in_line[8]);
		fprintf (stderr, "time = %ld usec\n",
			 (long) (clock () - start_time));
#endif
		if (error_code == 0)
			send (sockfd, node_name_ptr, strlen (node_name_ptr) + 1, 0);
		else if (error_code == EAGAIN)
			send (sockfd, "EAGAIN", 7, 0);
		else
			send (sockfd, "EINVAL", 7, 0);

		if (node_name_ptr)
			free (node_name_ptr);
	}
	else if (strncmp ("DumpBuild", in_line, 9) == 0) {
		start_time = clock ();
		error_code = dump_build (&dump, &dump_size);
#if DEBUG_SYSTEM
		if (error_code)
			fprintf (stderr,
				 "slurmctld_req: dump_build error %d, ",
				 error_code);
		else
			fprintf (stderr,
				 "slurmctld_req: dump_build returning %d bytes, ",
				 dump_size);
		fprintf (stderr, "time = %ld usec\n",
			 (long) (clock () - start_time));
#endif
		if (error_code == 0) {
			dump_loc = 0;
			while (dump_size > 0) {
				i = send (sockfd, &dump[dump_loc], dump_size,
					  0);
				dump_loc += i;
				dump_size -= i;
			}	
		}
		else
			send (sockfd, "EINVAL", 7, 0);
		if (dump)
			free (dump);
	}
	else if (strncmp ("DumpNode", in_line, 8) == 0) {
		start_time = clock ();
		time_stamp = NULL;
		error_code =
			load_string (&time_stamp, "LastUpdate=", in_line);
		if (time_stamp) {
			last_update = strtol (time_stamp, (char **) NULL, 10);
			free (time_stamp);
		}
		else
			last_update = (time_t) 0;
		error_code = dump_node (&dump, &dump_size, &last_update);
#if DEBUG_SYSTEM
		if (error_code)
			fprintf (stderr,
				 "slurmctld_req: dump_node error %d, ",
				 error_code);
		else
			fprintf (stderr,
				 "slurmctld_req: dump_node returning %d bytes, ",
				 dump_size);
		fprintf (stderr, "time = %ld usec\n",
			 (long) (clock () - start_time));
#endif
		if (dump_size == 0)
			send (sockfd, "nochange", 9, 0);
		else if (error_code == 0) {
			dump_loc = 0;
			while (dump_size > 0) {
				i = send (sockfd, &dump[dump_loc], dump_size,
					  0);
				dump_loc += i;
				dump_size -= i;
			}	
		}
		else
			send (sockfd, "EINVAL", 7, 0);
		if (dump)
			free (dump);
	}
	else if (strncmp ("DumpPart", in_line, 8) == 0) {
		start_time = clock ();
		time_stamp = NULL;
		error_code =
			load_string (&time_stamp, "LastUpdate=", in_line);
		if (time_stamp) {
			last_update = strtol (time_stamp, (char **) NULL, 10);
			free (time_stamp);
		}
		else
			last_update = (time_t) 0;
		error_code = dump_part (&dump, &dump_size, &last_update);
#if DEBUG_SYSTEM
		if (error_code)
			fprintf (stderr,
				 "slurmctld_req: dump_part error %d, ",
				 error_code);
		else
			fprintf (stderr,
				 "slurmctld_req: dump_part returning %d bytes, ",
				 dump_size);
		fprintf (stderr, "time = %ld usec\n",
			 (long) (clock () - start_time));
#endif
		if (dump_size == 0)
			send (sockfd, "nochange", 9, 0);
		else if (error_code == 0) {
			dump_loc = 0;
			while (dump_size > 0) {
				i = send (sockfd, &dump[dump_loc], dump_size,
					  0);
				dump_loc += i;
				dump_size -= i;
			}	
		}
		else
			send (sockfd, "EINVAL", 7, 0);
		if (dump)
			free (dump);
	}
	else if (strncmp ("JobSubmit", in_line, 9) == 0) {
		start_time = clock ();
		time_stamp = NULL;
		error_code = EINVAL;
#if DEBUG_SYSTEM
		if (error_code)
			fprintf (stderr, "slurmctld_req: job_submit error %d",
				 error_code);
		else
			fprintf (stderr,
				 "slurmctld_req: job_submit success for %s",
				 &in_line[10]);
		fprintf (stderr, "job_submit time = %ld usec\n",
			 (long) (clock () - start_time));
#endif
		if (error_code == 0)
			send (sockfd, dump, dump_size, 0);
		else
			send (sockfd, "EINVAL", 7, 0);
	}

	else if (strncmp ("JobWillRun", in_line, 10) == 0) {
		start_time = clock ();
		time_stamp = NULL;
		error_code = EINVAL;
#if DEBUG_SYSTEM
		if (error_code)
			fprintf (stderr,
				 "slurmctld_req: job_will_run error %d",
				 error_code);
		else
			fprintf (stderr,
				 "slurmctld_req: job_will_run success for %s",
				 &in_line[10]);
		fprintf (stderr, "job_will_run time = %ld usec\n",
			 (long) (clock () - start_time));
#endif
		if (error_code == 0)
			send (sockfd, dump, dump_size, 0);
		else
			send (sockfd, "EINVAL", 7, 0);
	}
	else if (strncmp ("NodeConfig", in_line, 10) == 0) {
		start_time = clock ();
		time_stamp = NULL;
		node_name_ptr = NULL;
		cpus = real_memory = tmp_disk = NO_VAL;
		error_code = load_string (&node_name_ptr, "NodeName=", in_line);
		if (node_name == NULL)
			error_code = EINVAL;
		if (error_code == 0)
			error_code = load_integer (&cpus, "CPUs=", in_line);
		if (error_code == 0)
			error_code =
				load_integer (&real_memory, "RealMemory=",
					      in_line);
		if (error_code == 0)
			error_code =
				load_integer (&tmp_disk, "TmpDisk=",
					      in_line);
		if (error_code == 0)
			error_code =
				validate_node_specs (node_name_ptr, cpus,
						     real_memory, tmp_disk);
#if DEBUG_SYSTEM
		if (error_code)
			fprintf (stderr,
				 "slurmctld_req: node_config error %d for %s",
				 error_code, node_name_ptr);
		else
			fprintf (stderr, "slurmctld_req: node_config for %s",
				 node_name_ptr);
		fprintf (stderr, "node_config time = %ld usec\n",
			 (long) (clock () - start_time));
#endif
		if (error_code == 0)
			send (sockfd, dump, dump_size, 0);
		else
			send (sockfd, "EINVAL", 7, 0);
		if (node_name_ptr)
			free (node_name_ptr);
	}
	else if (strncmp ("Reconfigure", in_line, 11) == 0) {
		start_time = clock ();
		time_stamp = NULL;
		error_code = init_slurm_conf ();
		if (error_code == 0)
			error_code = read_slurm_conf (SLURM_CONF);
#if DEBUG_SYSTEM
		if (error_code)
			fprintf (stderr,
				 "slurmctld_req: reconfigure error %d, ",
				 error_code);
		else
			fprintf (stderr,
				 "slurmctld_req: reconfigure completed successfully, ");
		fprintf (stderr, "time = %ld usec\n",
			 (long) (clock () - start_time));
#endif
		sprintf (in_line, "%d", error_code);
		send (sockfd, in_line, strlen (in_line) + 1, 0);
	}
	else if (strncmp ("Update", in_line, 6) == 0) {
		start_time = clock ();
		node_name_ptr = part_name = NULL;
		error_code = load_string (&node_name_ptr, "NodeName=", in_line);
		if ((error_code == 0) && (node_name_ptr != NULL))
			error_code = update_node (node_name_ptr, &in_line[6]);	/* skip "Update" */
		else {
			error_code =
				load_string (&part_name, "PartitionName=", in_line);
			if ((error_code == 0) && (part_name != NULL))
				error_code = update_part (part_name, &in_line[6]); /* skip "Update" */
			else
				error_code = EINVAL;
		}		
#if DEBUG_SYSTEM
		if (error_code) {
			if (node_name_ptr)
				fprintf (stderr,
					 "slurmctld_req: update error %d on node %s, ",
					 error_code, node_name_ptr);
			else if (part_name)
				fprintf (stderr,
					 "slurmctld_req: update error %d on partition %s, ",
					 error_code, part_name);
			else
				fprintf (stderr,
					 "slurmctld_req: update error %d on request %s, ",
					 error_code, in_line);

		}
		else {
			if (node_name_ptr)
				fprintf (stderr,
					 "slurmctld_req: updated node %s, ",
					 node_name_ptr);
			else
				fprintf (stderr,
					 "slurmctld_req: updated partition %s, ",
					 part_name);
		}		
		fprintf (stderr, "time = %ld usec\n",
			 (long) (clock () - start_time));
#endif
		sprintf (in_line, "%d", error_code);
		send (sockfd, in_line, strlen (in_line) + 1, 0);

		if (node_name_ptr)
			free (node_name_ptr);
		if (part_name)
			free (part_name);

	}
	else {
#if DEBUG_SYSTEM
		fprintf (stderr, "slurmctld_req: invalid request %s\n",
			 in_line);
#else
		syslog (LOG_WARNING, "slurmctld_req: invalid request %s\n",
			in_line);
#endif
		send (sockfd, "EINVAL", 7, 0);
	}			
	return;
}
