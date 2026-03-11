#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*
 * utils.h — Logging helpers and common error utilities for MiniRun.
 */

/* Coloured log levels */
#define ANSI_RESET  "\033[0m"
#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_RED    "\033[31m"
#define ANSI_CYAN   "\033[36m"

#define log_info(fmt, ...)  \
    fprintf(stderr, ANSI_GREEN  "[minirun][INFO ] " ANSI_RESET fmt "\n", ##__VA_ARGS__)

#define log_warn(fmt, ...)  \
    fprintf(stderr, ANSI_YELLOW "[minirun][WARN ] " ANSI_RESET fmt "\n", ##__VA_ARGS__)

#define log_error(fmt, ...) \
    fprintf(stderr, ANSI_RED    "[minirun][ERROR] " ANSI_RESET fmt "\n", ##__VA_ARGS__)

#define log_debug(fmt, ...) \
    fprintf(stderr, ANSI_CYAN   "[minirun][DEBUG] " ANSI_RESET fmt "\n", ##__VA_ARGS__)

/* Print strerror(errno) and exit */
#define die(msg)  do { perror(ANSI_RED "[minirun][FATAL] " msg ANSI_RESET); exit(EXIT_FAILURE); } while (0)

/* Check a syscall return value; die if < 0 */
#define CHECK(call, msg)  do { if ((call) < 0) { die(msg); } } while (0)

/* Parse a memory string like "256MB" or "512KB" → bytes */
long parse_memory(const char *s);

/* Parse a CPU percentage string like "50" → percent integer 1-100 */
int parse_cpu(const char *s);

#endif /* UTILS_H */
