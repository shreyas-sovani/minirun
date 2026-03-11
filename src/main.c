#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "container.h"
#include "cgroup.h"
#include "utils.h"

/*
 * main.c — MiniRun CLI entry point.
 *
 * Usage:
 *   sudo ./minirun [OPTIONS] <rootfs> <program> [args...]
 *
 * Options:
 *   --memory <size>   Memory limit (e.g. 256MB, 512KB, 1GB)
 *   --cpu    <pct>    CPU limit as percentage (e.g. 50 or 50%)
 *   --help            Show this help message
 *
 * Example:
 *   sudo ./minirun --memory 128MB --cpu 50 ./rootfs /bin/sh
 *   sudo ./minirun ./rootfs /bin/echo "Hello from container"
 */

static void print_usage(const char *argv0)
{
    fprintf(stderr,
        "\n"
        "  ╔══════════════════════════════════════════════════╗\n"
        "  ║   MiniRun — Minimal Container Runtime  v1.0.0   ║\n"
        "  ╚══════════════════════════════════════════════════╝\n"
        "\n"
        "  Usage:\n"
        "    sudo %s [OPTIONS] <rootfs> <program> [args...]\n"
        "\n"
        "  Options:\n"
        "    --memory <size>   Memory limit (default: unlimited)\n"
        "                      Units: B, KB, MB, GB  (e.g. 128MB)\n"
        "    --cpu    <pct>    CPU limit in percent (e.g. 50 or 50%%)\n"
        "    --help            Show this help\n"
        "\n"
        "  Examples:\n"
        "    sudo %s ./rootfs /bin/sh\n"
        "    sudo %s --memory 128MB --cpu 50 ./rootfs /bin/sh\n"
        "    sudo %s --memory 64MB ./rootfs /bin/echo Hello\n"
        "\n"
        "  Notes:\n"
        "    • Run with sudo — namespace creation requires CAP_SYS_ADMIN\n"
        "    • Create rootfs first:  bash setup_rootfs.sh\n"
        "\n",
        argv0, argv0, argv0, argv0);
}

int main(int argc, char *argv[])
{
    container_config_t cfg = {
        .hostname       = CONTAINER_HOSTNAME,
        .rootfs         = NULL,
        .argv           = NULL,
        .limits         = { .memory_bytes = 0, .cpu_percent = 0 },
    };

    int i = 1;

    /* --- Parse options --- */
    for (; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }

        if (strcmp(argv[i], "--memory") == 0) {
            if (i + 1 >= argc) {
                log_error("--memory requires a value (e.g. --memory 256MB)");
                return EXIT_FAILURE;
            }
            cfg.limits.memory_bytes = parse_memory(argv[++i]);
            if (cfg.limits.memory_bytes < 0) {
                log_error("invalid memory limit: \"%s\"", argv[i]);
                return EXIT_FAILURE;
            }
            continue;
        }

        if (strcmp(argv[i], "--cpu") == 0) {
            if (i + 1 >= argc) {
                log_error("--cpu requires a value (e.g. --cpu 50)");
                return EXIT_FAILURE;
            }
            cfg.limits.cpu_percent = parse_cpu(argv[++i]);
            if (cfg.limits.cpu_percent < 0) {
                log_error("invalid CPU limit: \"%s\"", argv[i]);
                return EXIT_FAILURE;
            }
            continue;
        }

        /* First non-option argument = rootfs path */
        if (argv[i][0] != '-') {
            break;
        }

        log_error("unknown option: %s  (try --help)", argv[i]);
        return EXIT_FAILURE;
    }

    /* --- Positional: rootfs --- */
    if (i >= argc) {
        log_error("missing <rootfs> argument");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    cfg.rootfs = argv[i++];

    /* --- Positional: program + args --- */
    if (i >= argc) {
        log_error("missing <program> argument");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    cfg.argv = &argv[i];   /* remaining argv slice: program + its args */

    /* --- Print effective configuration --- */
    fprintf(stderr, "\n");
    log_info("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    log_info("MiniRun container starting");
    log_info("  rootfs   : %s", cfg.rootfs);
    log_info("  program  : %s", cfg.argv[0]);
    log_info("  hostname : %s", cfg.hostname);
    if (cfg.limits.memory_bytes > 0)
        log_info("  memory   : %ld bytes (~%ld MiB)", cfg.limits.memory_bytes,
                 cfg.limits.memory_bytes / (1024 * 1024));
    else
        log_info("  memory   : unlimited");

    if (cfg.limits.cpu_percent > 0)
        log_info("  cpu      : %d%%", cfg.limits.cpu_percent);
    else
        log_info("  cpu      : unlimited");
    log_info("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    fprintf(stderr, "\n");

    /* --- Check root privileges --- */
    if (geteuid() != 0) {
        log_warn("not running as root — namespace operations may fail");
        log_warn("rerun with: sudo %s ...", argv[0]);
    }

    /* --- Launch container --- */
    int exit_code = container_run(&cfg);

    if (exit_code < 0) {
        log_error("container launch failed");
        return EXIT_FAILURE;
    }

    log_info("container finished with exit code %d", exit_code);
    return exit_code;
}
