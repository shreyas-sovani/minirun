#ifndef FS_H
#define FS_H

/*
 * fs.h — Filesystem sandbox setup for MiniRun.
 *
 * After clone() places us in a new mount namespace, fs_setup() performs:
 *   1. Bind-mount the rootfs onto itself (make it a mount point)
 *   2. Mount /proc inside the rootfs so the container can see its own pids
 *   3. pivot_root (or chroot fallback) into the rootfs
 *   4. Unmount the old root
 *
 * The result: the container process cannot see any path outside rootfs.
 */

/*
 * fs_setup - configure the filesystem sandbox inside the container.
 * @rootfs: absolute path to the directory to use as the container root.
 *
 * Must be called from the child process (inside the clone()d namespace).
 * Returns 0 on success, -1 on failure.
 */
int fs_setup(const char *rootfs);

#endif /* FS_H */
