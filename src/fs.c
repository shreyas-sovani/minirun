#define _GNU_SOURCE
#include "fs.h"
#include "utils.h"

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

/*
 * fs.c — Filesystem sandbox implementation.
 *
 * Strategy:
 *  1. Remount the entire host mount tree as MS_PRIVATE so our bind-mounts
 *     don't propagate back to the host.
 *  2. Bind-mount rootfs onto itself so it becomes an independent mount point
 *     (required by pivot_root).
 *  3. Mount a fresh procfs at <rootfs>/proc.
 *  4. Call pivot_root(rootfs, rootfs/old_root) to swap the root.
 *  5. Unmount and remove the old root.
 *  6. If pivot_root is unavailable (older kernels), fall back to chroot().
 */

/* Helper: write a small string to a path */
static int write_file(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        log_error("write_file open(%s): %s", path, strerror(errno));
        return -1;
    }
    ssize_t len = (ssize_t)strlen(value);
    if (write(fd, value, (size_t)len) != len) {
        log_error("write_file write(%s): %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

/* Helper: ensure a directory exists */
static int mkdir_p(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return 0;   /* already exists */
    if (mkdir(path, 0755) < 0 && errno != EEXIST) {
        log_error("mkdir(%s): %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

int fs_setup(const char *rootfs)
{
    char path[512];

    log_info("fs: setting up filesystem sandbox at %s", rootfs);

    /* 1. Make all mounts in current namespace private (no propagation to host) */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
        log_error("mount(MS_PRIVATE /): %s", strerror(errno));
        return -1;
    }

    /* 2. Bind-mount rootfs onto itself to make it a mount point */
    if (mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL) < 0) {
        log_error("bind-mount rootfs: %s", strerror(errno));
        return -1;
    }

    /* 3. Mount proc inside rootfs */
    snprintf(path, sizeof(path), "%s/proc", rootfs);
    mkdir_p(path);
    if (mount("proc", path, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0) {
        log_warn("mount proc: %s (continuing anyway)", strerror(errno));
        /* non-fatal — container may still work */
    }

    /* 4. Mount tmpfs on /tmp inside rootfs */
    snprintf(path, sizeof(path), "%s/tmp", rootfs);
    mkdir_p(path);
    if (mount("tmpfs", path, "tmpfs", MS_NOSUID | MS_NODEV, NULL) < 0) {
        log_warn("mount tmpfs at /tmp: %s", strerror(errno));
    }

    /* 5. pivot_root: requires an old_root inside rootfs */
    snprintf(path, sizeof(path), "%s/.old_root", rootfs);
    mkdir_p(path);

    /* pivot_root system call */
    if (syscall(SYS_pivot_root, rootfs, path) == 0) {
        log_info("fs: pivot_root succeeded");

        /* chdir to new root */
        if (chdir("/") < 0) { die("chdir / after pivot_root"); }

        /* Unmount the old root */
        if (umount2("/.old_root", MNT_DETACH) < 0) {
            log_warn("umount old_root: %s", strerror(errno));
        }
        rmdir("/.old_root");

    } else {
        /* Fallback: chroot (works without a real mount point) */
        log_warn("fs: pivot_root failed (%s), falling back to chroot", strerror(errno));

        if (chroot(rootfs) < 0) {
            log_error("chroot(%s): %s", rootfs, strerror(errno));
            return -1;
        }
        if (chdir("/") < 0) { die("chdir / after chroot"); }
    }

    log_info("fs: filesystem sandbox ready. New root = /");

    /* Set a reasonable umask inside the container */
    umask(022);

    /* Write simple /etc/hostname */
    mkdir_p("/etc");
    write_file("/etc/hostname", "minirun-container\n");

    return 0;
}
