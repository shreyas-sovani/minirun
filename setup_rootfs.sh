#!/usr/bin/env bash
# setup_rootfs.sh — Creates a minimal busybox-based rootfs for MiniRun
#
# Usage:  bash setup_rootfs.sh [rootfs_dir]
# Default rootfs directory: ./rootfs
#
# Requires: busybox (static build) on the host.
#   Install on Debian/Ubuntu: sudo apt-get install busybox-static
#   Install on Fedora/RHEL:   sudo dnf install busybox
#   Install on Arch:          sudo pacman -S busybox

set -euo pipefail

ROOTFS="${1:-./rootfs}"
BUSYBOX_BIN="$(command -v busybox 2>/dev/null || true)"

echo ""
echo "  ┌─────────────────────────────────────────────┐"
echo "  │  MiniRun rootfs setup                       │"
echo "  │  Target: ${ROOTFS}                          │"
echo "  └─────────────────────────────────────────────┘"
echo ""

# ─── Check busybox ───────────────────────────────────────────────────
if [ -z "$BUSYBOX_BIN" ]; then
    echo "  [ERROR] busybox not found on PATH."
    echo "  Install it with one of:"
    echo "    sudo apt-get install busybox-static   # Debian / Ubuntu"
    echo "    sudo dnf install busybox              # Fedora / RHEL"
    echo "    sudo pacman -S busybox                # Arch"
    exit 1
fi
echo "  [INFO] Using busybox: $BUSYBOX_BIN"

# ─── Check if we might already have a rootfs ─────────────────────────
if [ -d "$ROOTFS/bin" ]; then
    echo "  [WARN] $ROOTFS/bin already exists — skipping busybox install."
    echo "         Delete $ROOTFS and re-run to rebuild from scratch."
    exit 0
fi

# ─── Create directory structure ──────────────────────────────────────
echo "  [INFO] Creating directory structure..."
mkdir -p "$ROOTFS"/{bin,sbin,lib,lib64,usr/bin,usr/sbin,proc,sys,dev,tmp,etc,home,root,var/log}

# ─── Copy busybox static binary ──────────────────────────────────────
echo "  [INFO] Copying busybox to $ROOTFS/bin/busybox"
cp "$BUSYBOX_BIN" "$ROOTFS/bin/busybox"
chmod 755 "$ROOTFS/bin/busybox"

# ─── Install busybox applets as symlinks ─────────────────────────────
echo "  [INFO] Installing busybox symlinks..."
"$ROOTFS/bin/busybox" --install -s "$ROOTFS/bin"

# ─── Minimal /etc files ──────────────────────────────────────────────
echo "  [INFO] Writing /etc files..."

cat > "$ROOTFS/etc/hostname" <<EOF
minirun-container
EOF

cat > "$ROOTFS/etc/hosts" <<EOF
127.0.0.1   localhost
127.0.0.1   minirun-container
::1         localhost ip6-localhost ip6-loopback
EOF

cat > "$ROOTFS/etc/passwd" <<EOF
root:x:0:0:root:/root:/bin/sh
nobody:x:65534:65534:nobody:/:/bin/false
EOF

cat > "$ROOTFS/etc/group" <<EOF
root:x:0:
nobody:x:65534:
EOF

cat > "$ROOTFS/etc/shells" <<EOF
/bin/sh
/bin/ash
EOF

# ─── /etc/profile for a friendlier shell ─────────────────────────────
cat > "$ROOTFS/etc/profile" <<'EOF'
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export HOME=/root
export PS1='[minirun:\w]# '
alias ll='ls -la'
echo ""
echo "  Welcome to MiniRun container!"
echo "  Isolated PID namespace — you are PID $(cat /proc/self/status | grep ^Pid | awk '{print $2}')"
echo "  Type 'exit' to leave the container."
echo ""
EOF

# ─── /dev nodes (minimal) ────────────────────────────────────────────
echo "  [INFO] Creating /dev nodes..."
mkdir -p "$ROOTFS/dev"
# mknod requires root; attempt and warn if not
if [ "$(id -u)" -eq 0 ]; then
    mknod -m 666 "$ROOTFS/dev/null"    c 1 3  2>/dev/null || true
    mknod -m 666 "$ROOTFS/dev/zero"    c 1 5  2>/dev/null || true
    mknod -m 666 "$ROOTFS/dev/random"  c 1 8  2>/dev/null || true
    mknod -m 666 "$ROOTFS/dev/urandom" c 1 9  2>/dev/null || true
    mknod -m 666 "$ROOTFS/dev/tty"     c 5 0  2>/dev/null || true
    mknod -m 600 "$ROOTFS/dev/console" c 5 1  2>/dev/null || true
    echo "  [INFO] /dev nodes created (running as root)"
else
    echo "  [WARN] Not root: skipping /dev node creation."
    echo "         Re-run with sudo for full /dev support,"
    echo "         or the container will use a bind-mounted /dev from host."
fi

# ─── Done ─────────────────────────────────────────────────────────────
echo ""
echo "  ✓ rootfs created at: $ROOTFS"
echo ""
echo "  Directory layout:"
ls -la "$ROOTFS"
echo ""
echo "  Now build and run MiniRun:"
echo "    make"
echo "    sudo ./minirun $ROOTFS /bin/sh"
echo ""
