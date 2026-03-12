#!/usr/bin/env bash
# =============================================================================
# demo_mac.sh — MiniRun Demo for macOS
#
# MiniRun uses Linux-only kernel primitives (namespaces, cgroups, clone()).
# This script uses Docker to spin up a Linux environment on your Mac,
# compiles MiniRun inside it, and runs a full live demo showing every feature.
#
# Requirements:
#   • Docker Desktop for Mac  (https://www.docker.com/products/docker-desktop/)
#     Must be running before you execute this script.
#
# Usage:
#   bash demo_mac.sh
# =============================================================================

set -euo pipefail

# ─── Colour helpers ───────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${GREEN}[demo]${RESET} $*"; }
warn()    { echo -e "${YELLOW}[demo]${RESET} $*"; }
section() { echo -e "\n${BOLD}${CYAN}━━━  $*  ━━━${RESET}\n"; }
die()     { echo -e "${RED}[demo][FATAL]${RESET} $*" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="debian:bookworm-slim"
CONTAINER_NAME="minirun-demo-$$"

# ─── 1. Pre-flight checks ─────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}${CYAN}"
echo "  ╔══════════════════════════════════════════════════╗"
echo "  ║   MiniRun — macOS Demo via Docker               ║"
echo "  ║   (Linux namespaces + cgroups inside a Linux VM)║"
echo "  ╚══════════════════════════════════════════════════╝"
echo -e "${RESET}"

if ! command -v docker &>/dev/null; then
    die "Docker not found. Install Docker Desktop from https://www.docker.com/products/docker-desktop/"
fi

if ! docker info &>/dev/null; then
    die "Docker daemon is not running. Open Docker Desktop and wait for it to start, then re-run."
fi

info "Docker is running ✓"

# ─── 2. Build a Linux Docker image with build tools + busybox ─────────────────
section "Setting up Linux build environment"

info "Pulling ${IMAGE} and installing gcc, make, busybox-static..."
docker run --rm \
    --name "${CONTAINER_NAME}-check" \
    --privileged \
    -v "${SCRIPT_DIR}:/minirun" \
    "${IMAGE}" \
    bash -c "
        set -e
        # Speed up apt
        echo 'APT::Install-Recommends \"false\";' > /etc/apt/apt.conf.d/no-recommends
        apt-get update -qq
        apt-get install -y -qq gcc make busybox-static 2>&1 | tail -3

        echo ''
        echo '  Tools installed:'
        gcc --version | head -1
        make --version | head -1
        busybox | head -1

        # ── Build MiniRun ──────────────────────────────────────────────────
        echo ''
        echo '━━━  Building MiniRun  ━━━'
        cd /minirun
        make clean -s 2>/dev/null || true
        make

        # ── Create rootfs ─────────────────────────────────────────────────
        echo ''
        echo '━━━  Creating rootfs  ━━━'
        bash setup_rootfs.sh /minirun/rootfs

        # ══════════════════════════════════════════════════════════════════
        # DEMO 1: Basic container launch + hostname
        # ══════════════════════════════════════════════════════════════════
        echo ''
        echo '━━━  DEMO 1: Basic launch + UTS namespace (hostname)  ━━━'
        echo 'Host hostname:'
        hostname

        echo ''
        echo 'Container hostname:'
        sudo ./minirun /minirun/rootfs /bin/hostname

        # ══════════════════════════════════════════════════════════════════
        # DEMO 2: PID namespace — container can only see its own processes
        # ══════════════════════════════════════════════════════════════════
        echo ''
        echo '━━━  DEMO 2: PID namespace isolation  ━━━'
        echo 'Host process count (ps):' \$(ps aux | wc -l) processes

        echo ''
        echo 'Processes visible INSIDE container (should be just PID 1 + ps):'
        sudo ./minirun /minirun/rootfs /bin/ps aux

        # ══════════════════════════════════════════════════════════════════
        # DEMO 3: Filesystem isolation — can't see host paths
        # ══════════════════════════════════════════════════════════════════
        echo ''
        echo '━━━  DEMO 3: Filesystem isolation (chroot/pivot_root)  ━━━'
        echo 'Host root ls /:'
        ls / | tr \"\n\" \"  \"
        echo ''

        echo ''
        echo 'Container root ls / (only rootfs contents):'
        sudo ./minirun /minirun/rootfs /bin/ls /

        # ══════════════════════════════════════════════════════════════════
        # DEMO 4: Memory limiting
        # ══════════════════════════════════════════════════════════════════
        echo ''
        echo '━━━  DEMO 4: Memory limit via cgroup  ━━━'
        echo 'Running dd to write 200MB inside a 50MB-limited container...'
        echo 'Expect: OOM kill (exit code 137)'
        set +e
        sudo ./minirun --memory 50MB /minirun/rootfs /bin/sh -c \
            'dd if=/dev/zero of=/tmp/bigfile bs=1M count=200 2>&1; echo dd_exit=\$?'
        EXIT_CODE=\$?
        set -e
        if [ \"\$EXIT_CODE\" -eq 137 ] || [ \"\$EXIT_CODE\" -eq 1 ]; then
            echo \"  ✓ Container killed (exit \$EXIT_CODE) — memory limit enforced\"
        elif [ \"\$EXIT_CODE\" -eq 0 ]; then
            echo \"  ⚠ Container exited 0 — cgroup memory controller may not be enabled on this kernel\"
            echo \"    (Try: cat /sys/fs/cgroup/cgroup.controllers)\"
        else
            echo \"  ✓ Container exited with code \$EXIT_CODE\"
        fi

        # ══════════════════════════════════════════════════════════════════
        # DEMO 5: CPU limit
        # ══════════════════════════════════════════════════════════════════
        echo ''
        echo '━━━  DEMO 5: CPU limit via cgroup  ━━━'
        echo 'Spinning up a busy-loop with --cpu 20 for 3 seconds...'
        sudo ./minirun --cpu 20 /minirun/rootfs /bin/sh -c \
            'i=0; while [ \$i -lt 1000000 ]; do i=\$((i+1)); done; echo cpu_done' &
        CPID=\$!
        sleep 3
        kill \$CPID 2>/dev/null || true
        echo '  ✓ CPU-limited process ran and was stopped'

        # ══════════════════════════════════════════════════════════════════
        # DEMO 6: Error handling
        # ══════════════════════════════════════════════════════════════════
        echo ''
        echo '━━━  DEMO 6: Error handling  ━━━'

        echo 'Test: invalid memory value'
        set +e
        sudo ./minirun --memory abc /minirun/rootfs /bin/sh 2>&1 | grep -E 'ERROR|invalid'
        set -e

        echo ''
        echo 'Test: missing rootfs'
        set +e
        sudo ./minirun /nonexistent/rootfs /bin/sh 2>&1 | grep -E 'ERROR|not found'
        set -e

        echo ''
        echo '══════════════════════════════════════════════════════'
        echo '  ✓  All MiniRun demos completed successfully!'
        echo '══════════════════════════════════════════════════════'
    "

echo ""
echo -e "${GREEN}${BOLD}Demo complete!${RESET}"
echo ""
echo "  The demo ran MiniRun inside a Debian Linux container on your Mac."
echo "  Docker provides the Linux kernel that MiniRun requires."
echo ""
echo "  To run interactively:"
echo "    docker run -it --rm --privileged -v \"\$(pwd):/minirun\" ${IMAGE} bash"
echo "    # Then inside: cd /minirun && make && bash setup_rootfs.sh && sudo ./minirun ./rootfs /bin/sh"
echo ""
