#define _GNU_SOURCE
#include "cgroup.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

/*
 * cgroup.c — Resource control via Linux cgroup v1 / v2.
 *
 * Detection logic:
 *   If /sys/fs/cgroup/cgroup.controllers exists  → cgroup v2 (unified)
 *   Otherwise                                    → cgroup v1 (legacy)
 *
 * cgroup v1:
 *   /sys/fs/cgroup/memory/minirun-<name>/   (memory controller)
 *   /sys/fs/cgroup/cpu/minirun-<name>/      (cpu controller)
 *
 * cgroup v2:
 *   /sys/fs/cgroup/minirun-<name>/          (unified)
 */

#define CGROUPV1_MEM  "/sys/fs/cgroup/memory"
#define CGROUPV1_CPU  "/sys/fs/cgroup/cpu"
#define CGROUPV2_ROOT "/sys/fs/cgroup"

/* -----------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------- */

/* Write value to a cgroup file; silent on ENOENT (feature not available) */
static int cg_write(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            log_warn("cgroup file not found (skipping): %s", path);
            return 0;
        }
        log_error("cg_write open(%s): %s", path, strerror(errno));
        return -1;
    }
    ssize_t len = (ssize_t)strlen(value);
    ssize_t w   = write(fd, value, (size_t)len);
    close(fd);
    if (w != len) {
        log_error("cg_write write(%s, \"%s\"): %s", path, value, strerror(errno));
        return -1;
    }
    return 0;
}

/* Check whether cgroup v2 is active */
static int is_cgroupv2(void)
{
    struct stat st;
    return (stat(CGROUPV2_ROOT "/cgroup.controllers", &st) == 0) ? 1 : 0;
}

/* -----------------------------------------------------------------
 * cgroup v1
 * ----------------------------------------------------------------- */

static int cgv1_setup(const char *name, pid_t child_pid, const cgroup_limits_t *lim)
{
    char path[512];
    char val[64];

    /* --- Memory controller --- */
    if (lim->memory_bytes > 0) {
        snprintf(path, sizeof(path), "%s/%s", CGROUPV1_MEM, name);
        if (mkdir(path, 0755) < 0 && errno != EEXIST) {
            log_error("mkdir cgroup memory(%s): %s", path, strerror(errno));
            return -1;
        }
        log_info("cgroup v1: created memory cgroup %s", path);

        snprintf(val, sizeof(val), "%ld", lim->memory_bytes);
        snprintf(path, sizeof(path), "%s/%s/memory.limit_in_bytes", CGROUPV1_MEM, name);
        if (cg_write(path, val) < 0) return -1;

        /* Also set memsw (swap+mem) if available */
        snprintf(path, sizeof(path), "%s/%s/memory.memsw.limit_in_bytes", CGROUPV1_MEM, name);
        cg_write(path, val);   /* best-effort */

        snprintf(path, sizeof(path), "%s/%s/tasks", CGROUPV1_MEM, name);
        snprintf(val, sizeof(val), "%d", (int)child_pid);
        if (cg_write(path, val) < 0) return -1;

        log_info("cgroup v1: memory limit = %ld bytes", lim->memory_bytes);
    }

    /* --- CPU controller --- */
    if (lim->cpu_percent > 0) {
        snprintf(path, sizeof(path), "%s/%s", CGROUPV1_CPU, name);
        if (mkdir(path, 0755) < 0 && errno != EEXIST) {
            log_error("mkdir cgroup cpu(%s): %s", path, strerror(errno));
            return -1;
        }
        log_info("cgroup v1: created cpu cgroup %s", path);

        /* period = 100 000 µs (100 ms), quota = period * percent / 100 */
        long period = 100000L;
        long quota  = period * lim->cpu_percent / 100;

        snprintf(val, sizeof(val), "%ld", period);
        snprintf(path, sizeof(path), "%s/%s/cpu.cfs_period_us", CGROUPV1_CPU, name);
        if (cg_write(path, val) < 0) return -1;

        snprintf(val, sizeof(val), "%ld", quota);
        snprintf(path, sizeof(path), "%s/%s/cpu.cfs_quota_us", CGROUPV1_CPU, name);
        if (cg_write(path, val) < 0) return -1;

        snprintf(path, sizeof(path), "%s/%s/tasks", CGROUPV1_CPU, name);
        snprintf(val, sizeof(val), "%d", (int)child_pid);
        if (cg_write(path, val) < 0) return -1;

        log_info("cgroup v1: cpu limit = %d%% (quota=%ldµs / period=%ldµs)",
                 lim->cpu_percent, quota, period);
    }

    return 0;
}

static void cgv1_cleanup(const char *name)
{
    char path[512];

    snprintf(path, sizeof(path), "%s/%s", CGROUPV1_MEM, name);
    if (rmdir(path) < 0 && errno != ENOENT)
        log_warn("rmdir %s: %s", path, strerror(errno));

    snprintf(path, sizeof(path), "%s/%s", CGROUPV1_CPU, name);
    if (rmdir(path) < 0 && errno != ENOENT)
        log_warn("rmdir %s: %s", path, strerror(errno));
}

/* -----------------------------------------------------------------
 * cgroup v2 (unified hierarchy)
 * ----------------------------------------------------------------- */

static int cgv2_setup(const char *name, pid_t child_pid, const cgroup_limits_t *lim)
{
    char path[512];
    char val[128];

    /* Create the unified cgroup directory */
    snprintf(path, sizeof(path), "%s/%s", CGROUPV2_ROOT, name);
    if (mkdir(path, 0755) < 0 && errno != EEXIST) {
        log_error("mkdir cgroup v2 (%s): %s", path, strerror(errno));
        return -1;
    }
    log_info("cgroup v2: created unified cgroup %s", path);

    /* Enable subtree_control for memory and cpu from the parent cgroup.
     * This may already be set; ignore EINVAL / ENOENT. */
    int fd = open(CGROUPV2_ROOT "/cgroup.subtree_control", O_WRONLY);
    if (fd >= 0) {
        write(fd, "+memory +cpu", 12);
        close(fd);
    }

    /* Add child PID */
    snprintf(val, sizeof(val), "%d", (int)child_pid);
    snprintf(path, sizeof(path), "%s/%s/cgroup.procs", CGROUPV2_ROOT, name);
    if (cg_write(path, val) < 0) return -1;

    /* Memory limit */
    if (lim->memory_bytes > 0) {
        snprintf(val, sizeof(val), "%ld", lim->memory_bytes);
        snprintf(path, sizeof(path), "%s/%s/memory.max", CGROUPV2_ROOT, name);
        if (cg_write(path, val) < 0) return -1;

        /* Swap+mem */
        snprintf(path, sizeof(path), "%s/%s/memory.swap.max", CGROUPV2_ROOT, name);
        cg_write(path, val);  /* best-effort */

        log_info("cgroup v2: memory.max = %ld bytes", lim->memory_bytes);
    }

    /* CPU limit: "quota period" in µs */
    if (lim->cpu_percent > 0) {
        long period = 100000L;
        long quota  = period * lim->cpu_percent / 100;
        snprintf(val, sizeof(val), "%ld %ld", quota, period);
        snprintf(path, sizeof(path), "%s/%s/cpu.max", CGROUPV2_ROOT, name);
        if (cg_write(path, val) < 0) return -1;
        log_info("cgroup v2: cpu.max = %ld/%ld µs (%d%%)", quota, period, lim->cpu_percent);
    }

    return 0;
}

static void cgv2_cleanup(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", CGROUPV2_ROOT, name);
    if (rmdir(path) < 0 && errno != ENOENT)
        log_warn("rmdir cgroup v2 %s: %s", path, strerror(errno));
}

/* -----------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------- */

int cgroup_setup(const char *name, pid_t child_pid, const cgroup_limits_t *limits)
{
    /* If no limits requested, nothing to do */
    if (limits->memory_bytes <= 0 && limits->cpu_percent <= 0) {
        log_info("cgroup: no resource limits requested");
        return 0;
    }

    if (is_cgroupv2()) {
        log_info("cgroup: detected v2 (unified hierarchy)");
        return cgv2_setup(name, child_pid, limits);
    } else {
        log_info("cgroup: detected v1 (legacy hierarchy)");
        return cgv1_setup(name, child_pid, limits);
    }
}

void cgroup_cleanup(const char *name)
{
    if (is_cgroupv2()) {
        cgv2_cleanup(name);
    } else {
        cgv1_cleanup(name);
    }
    log_info("cgroup: cleanup done for %s", name);
}
