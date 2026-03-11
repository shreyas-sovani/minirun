#ifndef CGROUP_H
#define CGROUP_H

#include <sys/types.h>

/*
 * cgroup.h — Resource control via Linux Control Groups (cgroups).
 *
 * MiniRun supports both cgroup v1 and cgroup v2.
 *
 * cgroup v1 paths  (/sys/fs/cgroup/memory/... and /sys/fs/cgroup/cpu/...)
 * cgroup v2 paths  (/sys/fs/cgroup/...)
 *
 * Detection: if /sys/fs/cgroup/cgroup.controllers exists → v2, else v1.
 */

typedef struct {
    long memory_bytes;   /* 0 = unlimited */
    int  cpu_percent;    /* 0 = unlimited, 1-100 */
} cgroup_limits_t;

/*
 * cgroup_setup - create a cgroup for the container, apply limits,
 *               and add the child PID to it.
 *
 * @name: unique name for the cgroup (e.g. "minirun-<parent_pid>")
 * @child_pid: PID of the container child process
 * @limits: resource limits to apply
 *
 * Returns 0 on success, -1 on failure.
 */
int cgroup_setup(const char *name, pid_t child_pid, const cgroup_limits_t *limits);

/*
 * cgroup_cleanup - remove the cgroup directories created by cgroup_setup.
 * Safe to call even if setup failed partially.
 */
void cgroup_cleanup(const char *name);

#endif /* CGROUP_H */
