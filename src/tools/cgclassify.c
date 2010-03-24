/*
 * Copyright RedHat Inc. 2008
 *
 * Authors:	Vivek Goyal <vgoyal@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libcgroup.h>
#include <libcgroup-internal.h>
#include <limits.h>
#include <pwd.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "tools-common.h"

#define TEMP_BUF	81

/*
 * Change process group as specified on command line.
 */
int change_group_path(pid_t pid, struct cgroup_group_spec *cgroup_list[])
{
	int i;
	int ret = 0;

	for (i = 0; i < CG_HIER_MAX; i++) {
		if (!cgroup_list[i])
			break;

		ret = cgroup_change_cgroup_path(cgroup_list[i]->path, pid,
                                                (const char*const*) cgroup_list[i]->controllers);
		if (ret)
			fprintf(stderr, "Error changing group of pid %d: %s\n",
				pid, cgroup_strerror(ret));
			return -1;
	}

	return 0;
}

/*
 * Change process group as specified in cgrules.conf.
 */
int change_group_based_on_rule(pid_t pid)
{
	uid_t euid;
	gid_t egid;
	char *procname = NULL;
	int ret = -1;

	/* Put pid into right cgroup as per rules in /etc/cgrules.conf */
	if (cgroup_get_uid_gid_from_procfs(pid, &euid, &egid)) {
		fprintf(stderr, "Error in determining euid/egid of"
		" pid %d\n", pid);
		goto out;
	}
	ret = cgroup_get_procname_from_procfs(pid, &procname);
	if (ret) {
		fprintf(stderr, "Error in determining process name of"
		" pid %d\n", pid);
		goto out;
	}

	/* Change the cgroup by determining the rules */
	ret = cgroup_change_cgroup_flags(euid, egid, procname, pid, 0);
	if (ret) {
		fprintf(stderr, "Error: change of cgroup failed for"
		" pid %d: %s\n", pid, cgroup_strerror(ret));
		goto out;
	}
	ret = 0;
out:
	if (procname)
		free(procname);
	return ret;
}

static struct option longopts[] = {
	{"sticky", no_argument, NULL, 's'}, 
	{0, 0, 0, 0}
};

int main(int argc, char *argv[])
{
	int ret = 0, i, exit_code = 0;
	pid_t pid;
	int cg_specified = 0;
	int flag_child = 0;
	struct cgroup_group_spec *cgroup_list[CG_HIER_MAX];
	int c;


	if (argc < 2) {
		fprintf(stderr, "usage is %s "
			"[-g <list of controllers>:<relative path to cgroup>] "
			"[--sticky] <list of pids>  \n",
			argv[0]);
		exit(2);
	}

	memset(cgroup_list, 0, sizeof(cgroup_list));
	while ((c = getopt_long(argc, argv, "+g:s", longopts, NULL)) > 0) {
		switch (c) {
		case 'g':
			ret = parse_cgroup_spec(cgroup_list, optarg,
					CG_HIER_MAX);
			if (ret) {
				fprintf(stderr, "cgroup controller and path"
						"parsing failed\n");
				return -1;
			}
			cg_specified = 1;
			break;
		case 's':
			flag_child |= CGROUP_DAEMON_UNCHANGE_CHILDREN;
			break;
		default:
			fprintf(stderr, "Invalid command line option\n");
			exit(2);
			break;
		}
	}


	/* Initialize libcg */
	ret = cgroup_init();
	if (ret) {
		fprintf(stderr, "libcgroup initialization failed:%d\n", ret);
		return ret;
	}

	for (i = optind; i < argc; i++) {
		pid = (uid_t) atoi(argv[i]);

		if (flag_child)
			ret = cgroup_register_unchanged_process(pid, flag_child);
		if (ret)
			exit_code = 1;

		if (cg_specified)
			ret = change_group_path(pid, cgroup_list);
		else
			ret = change_group_based_on_rule(pid);

		/* if any group change fails */
		if (ret)
			exit_code = 1;
	}
	return exit_code;

}
