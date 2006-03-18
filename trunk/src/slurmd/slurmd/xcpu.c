/*****************************************************************************\
 *  src/slurmd/slurmd/xcpu.c - xcpu-based process management functions
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_XCPU

#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"

/* Write a message to a given file name, return 1 on success, 0 on failure */
static int _send_sig(char *path, int sig, char *msg)
{
	int fd, len, rc = 0;

	fd = open(path, O_WRONLY | O_APPEND);
	if (fd == -1)
		return 0;

	if (sig == 0)
		rc = 1;
	else {
		debug2("%s to %s", msg, path);
		len = strlen(msg) + 1;
		if (write(fd, msg, len) == len)
			rc = 1;
	}

	close(fd);
	return rc;
}

/* Identify every XCPU process in a specific node and signal it.
 * Return the process count */
extern int xcpu_signal(int sig, char *nodes)
{
	int procs = 0;
	hostlist_t hl;
	char *node, sig_msg[64], dir_path[128], ctl_path[200];
	DIR *dir;
	struct dirent *sub_dir;

	/* Translate "nodes" to a hostlist */
	hl = hostlist_create(nodes);
	if (hl == NULL) {
		error("hostlist_create: %m");
		return 0;
	}
	snprintf(sig_msg, sizeof(sig_msg), "signal %d", sig);

	/* For each node, look for processes */
	while (node = hostlist_shift(hl)) {
		snprintf(dir_path, sizeof(dir_path), 
			"%s/%s/xcpu",
			XCPU_DIR, node);
		free(node);
		if ((dir = opendir(dir_path)) == NULL) {
			error("opendir(%s): %m", dir_path);
			continue;
		}
		while ((sub_dir = readdir(dir))) {
			snprintf(ctl_path, sizeof(ctl_path),
				"%s/%s/ctl",dir_path, 
				sub_dir->d_name);
			procs += _send_sig(ctl_path, sig, sig_msg);
		}
		closedir(dir);
	}

	hostlist_destroy(hl);
	return procs;
}

#else

extern int xcpu_signal(int sig, char *nodes)
{
	return 0;
}
#endif
