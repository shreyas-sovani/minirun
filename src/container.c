#define _GNU_SOURCE
#include "container.h"
#include "cgroup.h"
#include "fs.h"
#include "utils.h"

#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>

/*
 * container.c — Container lifecycle:  clone → cgroup → wait → cleanup.
 *
 * Namespace flags used:
 *   CLONE_NEWPID   — new PID namespace (container PID 1 isolation)
 *   CLONE_NEWNS    — new mount namespace (private bind-mounts / pivot_root)
 *   CLONE_NEWUTS   — new UTS namespace (independent hostname)
 *   CLONE_NEWIPC   — new IPC namespace (isolated shared memory / semaphores)
 *
 * Parent flow:
 *   1. Allocate child stack
 *   2. clone(child_fn, ..., namespace_flags, cfg)
 *   3. Apply cgroup limits (attaches child PID to cgroup)
 *   4. waitpid() until child exits
 *   5. Cleanup cgroup
 *
 * Child flow (child_fn → child_exec):
 *   1. Receive config
 *   2. Set hostname (UTS namespace)
 *   3. Setup filesystem sandbox (mount ns + pivot_root/chroot)
 *   4. execvp() the target program → becomes PID 1 in its namespace
 */

/* ------------------------------------------------------------------ */
/* Child process entry point                                           */
/* ------------------------------------------------------------------ */

static int child_exec(void *arg)
{
    const container_config_t *cfg = (const container_config_t *)arg;

    log_info("container[child]: starting (PID inside ns = %d)", getpid());

    /* UTS namespace: set container hostname */
    if (sethostname(cfg->hostname, strlen(cfg->hostname)) < 0) {
        log_warn("sethostname: %s", strerror(errno));
    }
    log_info("container[child]: hostname set to \"%s\"", cfg->hostname);

    /* Mount namespace: set up filesystem sandbox */
    if (fs_setup(cfg->rootfs) < 0) {
        log_error("container[child]: filesystem setup failed");
        return EXIT_FAILURE;
    }

    /* Execute the requested program */
    log_info("container[child]: exec'ing: %s", cfg->argv[0]);
    execvp(cfg->argv[0], cfg->argv);

    /* If we reach here, execvp failed */
    log_error("container[child]: execvp(%s): %s", cfg->argv[0], strerror(errno));
    return EXIT_FAILURE;
}

/* ------------------------------------------------------------------ */
/* Parent: create child, apply cgroup, wait                           */
/* ------------------------------------------------------------------ */

int container_run(const container_config_t *cfg)
{
    /* Validate rootfs path */
    if (access(cfg->rootfs, F_OK) < 0) {
        log_error("rootfs directory not found: %s", cfg->rootfs);
        log_error("Run './setup_rootfs.sh' first to create a minimal rootfs.");
        return -1;
    }

    /* Allocate child stack (grows downward — pass top of buffer) */
    char *stack = (char *)malloc(CHILD_STACK_SIZE);
    if (!stack) { die("malloc child stack"); }
    char *stack_top = stack + CHILD_STACK_SIZE;

    log_info("container: cloning child with PID + mount + UTS + IPC namespaces");

    int clone_flags = CLONE_NEWPID   /* New PID namespace  */
                    | CLONE_NEWNS    /* New mount namespace */
                    | CLONE_NEWUTS   /* New UTS namespace   */
                    | CLONE_NEWIPC   /* New IPC namespace   */
                    | SIGCHLD;       /* Notify parent on death */

    pid_t child_pid = clone(child_exec, stack_top, clone_flags, (void *)cfg);
    if (child_pid < 0) {
        log_error("clone(): %s", strerror(errno));
        if (errno == EPERM) {
            log_error("Hint: re-run with sudo (namespace creation requires CAP_SYS_ADMIN)");
        }
        free(stack);
        return -1;
    }

    log_info("container: child PID on host = %d", child_pid);

    /* Apply cgroup limits — must happen after clone so we have the child PID */
    char cg_name[64];
    snprintf(cg_name, sizeof(cg_name), "minirun-%d", (int)getpid());

    if (cgroup_setup(cg_name, child_pid, &cfg->limits) < 0) {
        log_warn("cgroup setup failed — running without resource limits");
        /* Non-fatal: continue so the container still runs */
    }

    /* Wait for the child to finish */
    int wstatus;
    pid_t waited = waitpid(child_pid, &wstatus, 0);
    if (waited < 0) {
        log_error("waitpid: %s", strerror(errno));
        free(stack);
        cgroup_cleanup(cg_name);
        return -1;
    }

    /* Cleanup cgroup */
    cgroup_cleanup(cg_name);
    free(stack);

    /* Decode exit status and return it */
    if (WIFEXITED(wstatus)) {
        int code = WEXITSTATUS(wstatus);
        log_info("container: exited normally with code %d", code);
        return code;
    } else if (WIFSIGNALED(wstatus)) {
        int sig = WTERMSIG(wstatus);
        if (sig == SIGKILL) {
            log_warn("container: killed by SIGKILL (likely OOM — memory limit exceeded)");
        } else {
            log_warn("container: killed by signal %d (%s)", sig, strsignal(sig));
        }
        return 128 + sig;   /* Shell convention: 128 + signal number */
    }

    return -1;
}
