/* Boot log, self-checks and crash dump for hardware runs.
 *
 * Everything here is deliberately dependency-free: it writes to the SD card
 * with plain stdio (and uses one raw fd in the exception handler), so it still
 * works when the renderer, audio or romfs failed to come up.  main.c draws the
 * report; this module only collects the facts.
 */
#pragma once

#include <3ds.h>

enum {
    DIAG_OK = 0,
    DIAG_FAIL = 1,
    DIAG_WARN = 2,
};

/* Call this before romfsInit/gfxInitDefault: it makes sdmc:/3ds/sts3ds and
 * rotates the previous log to log.prev.txt.  Safe to call once. */
void diag_init(void);
void diag_shutdown(void);

/* Every line is timestamped and flushed, so the last line survives a hard
 * reset.  Do not call from an interrupt handler. */
void diag_log(const char *fmt, ...);
void diag_stage(const char *name);

/* A check row is shown on the boot/overlay screens and written to the log.
 * diag_warn is for optional pieces (music, effects) that should not stop a
 * run. */
void diag_check(const char *name, int ok, const char *detail);
void diag_warn(const char *name, const char *detail);

int diag_fail_count(void);
int diag_warn_count(void);
int diag_row_count(void);
const char *diag_row_name(int index);
const char *diag_row_detail(int index);
int diag_row_state(int index);

/* The last few log lines, for the on-console overlay. */
int diag_tail_count(void);
const char *diag_tail_line(int index);

const char *diag_last_stage(void);
const char *diag_log_path(void);
const char *diag_crash_path(void);
