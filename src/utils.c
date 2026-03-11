#include "utils.h"
#include <ctype.h>

/*
 * utils.c — Logging helpers and argument parsers.
 */

/*
 * parse_memory — accepts strings like:
 *   "256"    → 256 MiB (default unit)
 *   "256MB"  → 256 MiB
 *   "512KB"  → 512 KiB
 *   "1GB"    → 1 GiB
 * Returns the value in bytes, or -1 on parse error.
 */
long parse_memory(const char *s)
{
    if (!s || !*s) return -1;

    char *end;
    long val = strtol(s, &end, 10);
    if (end == s || val <= 0) return -1;

    /* Skip optional whitespace */
    while (isspace((unsigned char)*end)) end++;

    if (*end == '\0' || strcasecmp(end, "mb") == 0 || strcasecmp(end, "m") == 0) {
        return val * 1024L * 1024L;                 /* default: MiB */
    } else if (strcasecmp(end, "kb") == 0 || strcasecmp(end, "k") == 0) {
        return val * 1024L;
    } else if (strcasecmp(end, "gb") == 0 || strcasecmp(end, "g") == 0) {
        return val * 1024L * 1024L * 1024L;
    } else if (strcasecmp(end, "b") == 0) {
        return val;
    }

    log_error("unknown memory unit: \"%s\" (use B, KB, MB, GB)", end);
    return -1;
}

/*
 * parse_cpu — accepts "50" or "50%" → integer in [1, 100].
 * Returns -1 on error.
 */
int parse_cpu(const char *s)
{
    if (!s || !*s) return -1;

    char *end;
    long val = strtol(s, &end, 10);

    /* Strip optional '%' */
    while (isspace((unsigned char)*end)) end++;
    if (*end == '%') end++;

    if (*end != '\0') {
        log_error("invalid CPU percentage: \"%s\"", s);
        return -1;
    }
    if (val < 1 || val > 100) {
        log_error("CPU percentage must be between 1 and 100 (got %ld)", val);
        return -1;
    }
    return (int)val;
}
