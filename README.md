# MiniRun — Minimal Container Runtime

> A lightweight container runtime built from scratch in C, demonstrating core Operating System principles: process isolation via Linux **namespaces**, resource control via **cgroups**, and filesystem sandboxing via **pivot_root / chroot**.

```
  ╔══════════════════════════════════════════════════╗
  ║   MiniRun — Minimal Container Runtime  v1.0.0   ║
  ╚══════════════════════════════════════════════════╝
```

---

## Table of Contents

1. [Overview](#overview)
2. [OS Concepts Demonstrated](#os-concepts-demonstrated)
3. [Architecture](#architecture)
4. [Project Structure](#project-structure)
5. [Prerequisites](#prerequisites)
6. [Build Instructions](#build-instructions)
7. [Setup: Creating the Rootfs](#setup-creating-the-rootfs)
8. [Usage](#usage)
9. [Demo Walkthrough](#demo-walkthrough)
10. [How Each Feature Works (Deep Dive)](#how-each-feature-works-deep-dive)
11. [Expected Output](#expected-output)
12. [Troubleshooting](#troubleshooting)
13. [Future Enhancements](#future-enhancements)

---

## Overview

MiniRun executes programs in an **isolated container environment** without Docker, systemd, or any container daemon. It directly invokes Linux kernel primitives:

| Feature              | Kernel Mechanism                        |
|----------------------|-----------------------------------------|
| Process isolation    | `clone()` + `CLONE_NEWPID`              |
| Filesystem sandbox   | `pivot_root()` / `chroot()`             |
| Mount isolation      | `CLONE_NEWNS` + bind mounts             |
| Hostname isolation   | `CLONE_NEWUTS` + `sethostname()`        |
| IPC isolation        | `CLONE_NEWIPC`                          |
| Memory limiting      | cgroup `memory.limit_in_bytes` (v1) or `memory.max` (v2) |
| CPU throttling       | cgroup `cpu.cfs_quota_us` (v1) or `cpu.max` (v2) |

> **Platform requirement:** Linux only (macOS lacks Linux namespaces + cgroups).  
> **On macOS: use the one-command Docker demo — see [Running on macOS](#running-on-macos) below.**

---

## Running on macOS

MiniRun uses Linux kernel primitives that don't exist on macOS. The included `demo_mac.sh` script handles everything automatically using **Docker Desktop** (which runs a Linux VM under the hood).

### Quick start (one command)

```bash
# 1. Make sure Docker Desktop is running
# 2. Then:
bash demo_mac.sh
```

That's it. The script will:
1. Pull a Debian Linux container image
2. Install `gcc`, `make`, and `busybox-static` inside it
3. Compile MiniRun
4. Create the rootfs
5. Run **6 live demos** showing every isolation feature:
   - Hostname isolation (UTS namespace)
   - PID isolation (container sees only PID 1)
   - Filesystem isolation (container can't see host `/`)
   - Memory limit enforcement (OOM kill at 50 MB)
   - CPU throttling (busy-loop capped at 20%)
   - Error handling (bad args, missing rootfs)

### To get an interactive shell inside the Linux container

```bash
docker run -it --rm --privileged \
  -v "$(pwd):/minirun" \
  debian:bookworm-slim bash

# Then inside:
apt-get update && apt-get install -y gcc make busybox-static
cd /minirun
make
bash setup_rootfs.sh
sudo ./minirun ./rootfs /bin/sh
```

> **Why `--privileged`?**  
> Namespace creation requires `CAP_SYS_ADMIN`. `--privileged` grants this inside the Docker Linux VM.

---

## OS Concepts Demonstrated

### 1. Process Lifecycle Management
MiniRun calls `clone()` (not `fork()`) to create a child process with completely new namespaces. The child becomes **PID 1** inside its own PID namespace, just like init inside a real container.

### 2. Namespace-Based Resource Isolation
Linux namespaces partition global OS resources so each container sees its own view:

| Namespace   | Flag           | What it Isolates                              |
|-------------|----------------|-----------------------------------------------|
| PID         | `CLONE_NEWPID` | Process IDs — container sees only its own PIDs|
| Mount       | `CLONE_NEWNS`  | Filesystem mounts — independent mount table   |
| UTS         | `CLONE_NEWUTS` | Hostname and domain name                      |
| IPC         | `CLONE_NEWIPC` | System V IPC + POSIX message queues           |

### 3. Filesystem Abstraction and Sandboxing
The runtime:
1. Bind-mounts the rootfs onto itself (making it an independent mount point).
2. Mounts a fresh `procfs` inside it so `ps`, `top`, etc. show only container PIDs.
3. Calls `pivot_root()` to swap the root filesystem (or falls back to `chroot()`).
4. Unmounts and removes the old root — the process can no longer reach host paths.

### 4. Operating System Resource Allocation (cgroups)
Control groups let the kernel enforce hard resource limits:

- **Memory**: `memory.limit_in_bytes` (v1) / `memory.max` (v2) — process is OOM-killed if it exceeds the limit.
- **CPU**: `cpu.cfs_quota_us` / `cpu.cfs_period_us` (v1) — sets the CPU bandwidth allocation. E.g., `--cpu 50` → 50 ms runtime per 100 ms period.

MiniRun auto-detects cgroup v1 vs v2 by checking for `/sys/fs/cgroup/cgroup.controllers`.

### 5. Kernel System Call Interaction
The implementation directly uses `syscall(SYS_pivot_root, ...)`, `mount()`, `clone()`, `sethostname()`, `umount2()` — zero library abstractions over the kernel ABI.

### 6. Security Through Process Isolation
A process inside the container cannot:
- See host processes (different PID namespace)
- Access host filesystem paths (sandboxed root)
- Consume unbounded CPU/memory (cgroup limits)
- Affect host IPC objects

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    HOST SYSTEM                          │
│                                                         │
│  minirun (parent process)                               │
│  ├── Parses CLI args → container_config_t               │
│  ├── Calls clone() with namespace flags                 │
│  ├── Writes child PID to cgroup → applies limits        │
│  └── waitpid() → cleanup → exit with container code    │
│                                                         │
│  ┌──────────────────────────────────────────────────┐   │
│  │  CHILD PROCESS  (new PID, mount, UTS, IPC ns)   │   │
│  │                                                  │   │
│  │  ① sethostname("minirun-container")              │   │
│  │  ② mount(MS_PRIVATE) — private mount tree        │   │
│  │  ③ bind-mount rootfs onto itself                 │   │
│  │  ④ mount procfs at rootfs/proc                   │   │
│  │  ⑤ pivot_root(rootfs, rootfs/.old_root)          │   │
│  │  ⑥ umount2("/.old_root", MNT_DETACH)            │   │
│  │  ⑦ execvp(program, args) → becomes PID 1        │   │
│  │                                                  │   │
│  └──────────────────────────────────────────────────┘   │
│                                                         │
│  cgroup (enforced by kernel):                           │
│    /sys/fs/cgroup/.../minirun-<pid>/                    │
│    memory.limit_in_bytes = N                            │
│    cpu.cfs_quota_us = M                                 │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### Data Flow

```
main()
  └─ parse CLI args
       └─ container_run(cfg)
            ├─ parent: clone(child_exec, flags)
            ├─ parent: cgroup_setup(name, child_pid, limits)
            ├─ parent: waitpid(child_pid)
            ├─ parent: cgroup_cleanup(name)
            └─ child_exec():
                 ├─ sethostname()
                 ├─ fs_setup(rootfs) → pivot_root / chroot
                 └─ execvp(program, args)
```

---

## Project Structure

```
minirun/
├── Makefile                   # Build system
├── README.md                  # This file
├── setup_rootfs.sh            # Bootstrap script for busybox rootfs
├── rootfs/                    # Created by setup_rootfs.sh
│   ├── bin/                   # busybox + symlinked applets
│   ├── proc/                  # procfs mount point
│   ├── tmp/                   # tmpfs mount point
│   ├── dev/                   # device nodes
│   └── etc/                   # hostname, hosts, passwd, profile
└── src/
    ├── main.c                 # Entry point — CLI parsing
    ├── container.c / .h       # clone(), namespace setup, wait
    ├── cgroup.c / .h          # cgroup v1/v2 resource limits
    ├── fs.c / .h              # pivot_root / chroot filesystem sandbox
    └── utils.c / .h           # Logging, argument parsers
```

---

## Prerequisites

**Operating System:** Linux (kernel ≥ 3.8 for namespace support, ≥ 4.6 for cgroup v2)

| Dependency          | Purpose                       | Install                        |
|---------------------|-------------------------------|--------------------------------|
| `gcc`               | C compiler                    | `sudo apt install gcc`         |
| `make`              | Build system                  | `sudo apt install make`        |
| `busybox-static`    | Container rootfs (applets)    | `sudo apt install busybox-static` |
| `sudo` / root       | Namespace + cgroup access     | Built-in                       |

On **WSL2** with Ubuntu:
```bash
sudo apt update && sudo apt install gcc make busybox-static
```

On **Fedora / RHEL**:
```bash
sudo dnf install gcc make busybox
```

On **Arch Linux**:
```bash
sudo pacman -S gcc make busybox
```

---

## Build Instructions

```bash
# Clone / navigate to the project
cd /path/to/minirun

# Build
make

# Expected output:
#   ✓ Build successful → ./minirun
```

To clean build artifacts:
```bash
make clean
```

---

## Setup: Creating the Rootfs

MiniRun needs a minimal Linux filesystem to use as the container root. The provided script creates one using **BusyBox** statically compiled with 400+ common Unix utilities.

```bash
# Run once before first use
bash setup_rootfs.sh

# Or specify a custom path
bash setup_rootfs.sh /tmp/my-rootfs
```

The script creates:
```
rootfs/
├── bin/          ← busybox + sh, ls, ps, echo, cat, kill... (symlinks)
├── proc/         ← mount point for procfs
├── tmp/          ← mount point for tmpfs
├── dev/          ← null, zero, random, urandom, tty
└── etc/
    ├── hostname  ← "minirun-container"
    ├── hosts     ← 127.0.0.1 localhost
    ├── passwd    ← root entry
    └── profile   ← welcome banner + PATH
```

---

## Usage

### Synopsis
```
sudo ./minirun [OPTIONS] <rootfs> <program> [args...]
```

### Options

| Option               | Description                         | Example          |
|----------------------|-------------------------------------|------------------|
| `--memory <size>`    | Memory limit (B, KB, MB, GB)        | `--memory 128MB` |
| `--cpu <percent>`    | CPU limit as percentage (1–100)     | `--cpu 50`       |
| `--help`             | Show help message                   |                  |

### Examples

```bash
# Interactive shell with no limits
sudo ./minirun ./rootfs /bin/sh

# Shell with 128 MB memory limit and 50% CPU limit
sudo ./minirun --memory 128MB --cpu 50 ./rootfs /bin/sh

# Run a single command
sudo ./minirun ./rootfs /bin/echo "Hello from container!"

# Python script (if Python is in your rootfs)
sudo ./minirun --memory 256MB ./rootfs /usr/bin/python3 /scripts/test.py

# Stress test with limits
sudo ./minirun --memory 50MB --cpu 25 ./rootfs /bin/sh -c "dd if=/dev/zero bs=1M count=200"
```

---

## Demo Walkthrough

Follow these steps in order on a Linux system to observe each isolation feature.

### Step 0: Build and Setup
```bash
cd minirun
make
bash setup_rootfs.sh
```

### Step 1: Basic Container Launch
```bash
sudo ./minirun ./rootfs /bin/sh
```
You should see the MiniRun startup banner and drop into a shell.

### Step 2: Verify PID Isolation
Inside the container shell:
```sh
ps aux
# Expected: only sh at PID 1. No host processes visible.
echo "My PID is: $$"
# Expected: 1
```

### Step 3: Verify Filesystem Isolation
```sh
ls /
# Expected: bin  dev  etc  proc  root  sys  tmp
# NOT: boot, home, snap, Users, etc. (host directories)

cat /etc/hostname
# Expected: minirun-container

ls /proc/1/
# Expected: container's own process info
```

### Step 4: Verify Hostname Isolation
```sh
hostname
# Expected: minirun-container
# (While host hostname remains unchanged)
```

### Step 5: Verify Memory Limiting (needs cgroup v1/v2 support)
```bash
# In a new terminal on the host, run:
sudo ./minirun --memory 50MB ./rootfs /bin/sh -c "dd if=/dev/zero of=/tmp/bigfile bs=1M count=200"
# Expected: process is killed by the Linux OOM killer
# Exit code: 137 (128 + SIGKILL)
```

### Step 6: Verify CPU Limiting
```bash
# Launch a CPU-intensive process with 20% limit:
sudo ./minirun --cpu 20 ./rootfs /bin/sh -c "while true; do :; done" &

# On host, check cpu usage:
top -p $(pgrep minirun | tail -1)
# Expected: ~20% CPU, not 100%

kill %1   # stop the background job
```

### Step 7: Error Handling
```bash
# Invalid memory unit
sudo ./minirun --memory abc ./rootfs /bin/sh
# Expected: [minirun][ERROR] invalid memory limit: "abc"

# Missing rootfs
sudo ./minirun /nonexistent /bin/sh
# Expected: [minirun][ERROR] rootfs directory not found

# Insufficient privileges
./minirun ./rootfs /bin/sh   # without sudo
# Expected: [minirun][WARN] not running as root...
```

---

## How Each Feature Works (Deep Dive)

### `clone()` vs `fork()`
```c
// fork() creates a copy of the calling process, sharing namespaces
pid_t pid = fork();

// clone() creates a child with NEW namespaces — the container mechanism
pid_t pid = clone(child_fn, stack_top,
                  CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | SIGCHLD,
                  (void *)config);
```
The ns flags create fresh kernel namespace objects for the child. The host retains its original namespaces.

### `pivot_root()` vs `chroot()`
`chroot()` changes the root as seen by the process, but the old `/` is still accessible via `/proc/self/root` and through bind mounts. `pivot_root()` is more secure:
```c
// 1. Make the mount tree private (no propagation)
mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);

// 2. Bind-mount rootfs to make it a proper mount point  
mount(rootfs, rootfs, NULL, MS_BIND | MS_REC, NULL);

// 3. pivot_root: old root moves to rootfs/.old_root
syscall(SYS_pivot_root, rootfs, old_root_path);

// 4. Detach and remove old root (host filesystem gone from view)
umount2("/.old_root", MNT_DETACH);
rmdir("/.old_root");
```

### cgroup Resource Limits
After `clone()`, the parent assigns the child PID to a cgroup:
```
# cgroup v1:
echo <child_pid> > /sys/fs/cgroup/memory/minirun-<N>/tasks
echo 134217728   > /sys/fs/cgroup/memory/minirun-<N>/memory.limit_in_bytes

echo <child_pid> > /sys/fs/cgroup/cpu/minirun-<N>/tasks
echo 100000      > /sys/fs/cgroup/cpu/minirun-<N>/cpu.cfs_period_us
echo 50000       > /sys/fs/cgroup/cpu/minirun-<N>/cpu.cfs_quota_us  # 50%

# cgroup v2:
echo <child_pid> > /sys/fs/cgroup/minirun-<N>/cgroup.procs
echo 134217728   > /sys/fs/cgroup/minirun-<N>/memory.max
echo "50000 100000" > /sys/fs/cgroup/minirun-<N>/cpu.max
```

---

## Expected Output

```
$ sudo ./minirun --memory 128MB --cpu 50 ./rootfs /bin/sh

[minirun][INFO ] ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
[minirun][INFO ] MiniRun container starting
[minirun][INFO ]   rootfs   : ./rootfs
[minirun][INFO ]   program  : /bin/sh
[minirun][INFO ]   hostname : minirun-container
[minirun][INFO ]   memory   : 134217728 bytes (~128 MiB)
[minirun][INFO ]   cpu      : 50%
[minirun][INFO ] ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

[minirun][INFO ] container: cloning child with PID + mount + UTS + IPC namespaces
[minirun][INFO ] container: child PID on host = 4892
[minirun][INFO ] cgroup: detected v2 (unified hierarchy)
[minirun][INFO ] cgroup v2: created unified cgroup /sys/fs/cgroup/minirun-4891
[minirun][INFO ] cgroup v2: memory.max = 134217728 bytes
[minirun][INFO ] cgroup v2: cpu.max = 50000/100000 µs (50%)
[minirun][INFO ] container[child]: starting (PID inside ns = 1)
[minirun][INFO ] container[child]: hostname set to "minirun-container"
[minirun][INFO ] fs: setting up filesystem sandbox at ./rootfs
[minirun][INFO ] fs: pivot_root succeeded
[minirun][INFO ] fs: filesystem sandbox ready. New root = /
[minirun][INFO ] container[child]: exec'ing: /bin/sh

  Welcome to MiniRun container!
  Isolated PID namespace — you are PID 1
  Type 'exit' to leave the container.

[minirun-container:/]# ps aux
PID   USER     COMMAND
    1 root     /bin/sh
    3 root     ps aux
[minirun-container:/]#
```

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|----------|
| `clone(): Operation not permitted` | Missing root/CAP_SYS_ADMIN | Run with `sudo` |
| `rootfs directory not found` | setup_rootfs.sh not run | `bash setup_rootfs.sh` |
| `busybox not found` | busybox not installed | `sudo apt install busybox-static` |
| `mount proc: Permission denied` | Kernel restriction | Ensure you're root |
| Memory limit has no effect | cgroup not mounted | Check `mount | grep cgroup` |
| CPU limit has no effect | cgroup CPU controller disabled | `cat /sys/fs/cgroup/cgroup.controllers` — must include `cpu` |
| `invalid memory limit` | Bad format | Use: `128MB`, `512KB`, `1GB` |
| Container can see host / | pivot_root failed, chroot used | Normal fallback — check dmesg for details |

### Verify cgroup is active
```bash
# Check cgroup version
stat -fc %T /sys/fs/cgroup/

# v2: outputs "cgroup2fs"
# v1: outputs "tmpfs"

# List cgroup controllers available
cat /sys/fs/cgroup/cgroup.controllers  # v2
# or
ls /sys/fs/cgroup/                     # v1: shows memory, cpu, etc.
```

---

## Future Enhancements

Aligned with the PRD's future scope:

| Feature | Mechanism |
|---------|-----------|
| Network namespace | `CLONE_NEWNET` + `veth` pairs + NAT |
| Container images | OCI image spec + layer extraction |
| Filesystem layering | OverlayFS (`overlay` mount type) |
| User namespace | `CLONE_NEWUSER` (rootless containers) |
| Security policies | seccomp-bpf syscall filtering |
| Container orchestration | Control plane managing lifecycle |

---

## Academic References

- Linux `clone(2)` man page
- Linux `namespaces(7)` man page
- Linux `cgroups(7)` man page
- *Linux Kernel Development* — Robert Love
- *Container Security* — Liz Rice
- OCI Runtime Specification — https://opencontainers.org/

---

## License

MIT — Educational use. Built as an OS course project demonstrating kernel-level container primitives.
