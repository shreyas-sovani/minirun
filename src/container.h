#ifndef CONTAINER_H
#define CONTAINER_H

#include "cgroup.h"

/*
 * container.h — Container lifecycle management for MiniRun.
 *
 * container_run() is the top-level entry:
 *   1. Creates a child process via clone() with namespace flags.
 *   2. Applies cgroup limits from the parent.
 *   3. Waits for the child and cleans up.
 *
 * The child executes child_exec() which sets up the filesystem
 * sandbox and exec()s the target program.
 */

#define CONTAINER_HOSTNAME "minirun-container"
#define CHILD_STACK_SIZE   (1024 * 1024)   /* 1 MiB */

/* Configuration passed to container_run */
typedef struct {
    const char  *rootfs;          /* path to rootfs directory       */
    const char  *hostname;        /* UTS hostname for the container */
    char *const *argv;            /* program + args to execute      */
    cgroup_limits_t limits;       /* resource limits                */
} container_config_t;

/*
 * container_run — launches and supervises a container.
 * Returns the exit status of the contained program, or -1 on error.
 */
int container_run(const container_config_t *cfg);

#endif /* CONTAINER_H */
