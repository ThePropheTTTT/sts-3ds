#include "diag.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DIAG_DIR       "sdmc:/3ds/sts3ds"
#define DIAG_LOG       DIAG_DIR "/log.txt"
#define DIAG_LOG_PREV  DIAG_DIR "/log.prev.txt"
#define DIAG_CRASH     DIAG_DIR "/crash.txt"
#define DIAG_CRASH_PREV DIAG_DIR "/crash.prev.txt"

#define DIAG_MAX_ROWS 40
#define DIAG_TAIL_LINES 8

typedef struct {
    char name[56];
    char detail[80];
    unsigned char state;
} DiagRow;

static FILE *s_log;
static int s_crash_fd = -1;
static u64 s_start_tick;
static char s_stage[80] = "boot";
static DiagRow s_rows[DIAG_MAX_ROWS];
static int s_row_count;
static int s_fail_count;
static int s_warn_count;
static char s_tail[DIAG_TAIL_LINES][160];
static int s_tail_count;
static int s_tail_next;

static u8 s_exc_stack[8192] __attribute__((aligned(8)));
static ERRF_ExceptionData s_exc_data;

static unsigned long elapsed_ms(void) {
    u64 now = svcGetSystemTick();
    return (unsigned long)((now - s_start_tick) / (u64)(SYSCLOCK_ARM11 / 1000));
}

static void tail_add(const char *text) {
    size_t n = text ? strlen(text) : 0;
    if (n >= sizeof(s_tail[0])) n = sizeof(s_tail[0]) - 1;
    if (n) memcpy(s_tail[s_tail_next], text, n);
    s_tail[s_tail_next][n] = 0;
    s_tail_next = (s_tail_next + 1) % DIAG_TAIL_LINES;
    if (s_tail_count < DIAG_TAIL_LINES) s_tail_count++;
}

void diag_log(const char *fmt, ...) {
    char text[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

    tail_add(text);
    if (s_log) {
        unsigned long ms = elapsed_ms();
        fprintf(s_log, "%lu.%03lu %s\n", ms / 1000UL, ms % 1000UL, text);
        fflush(s_log);
    }
}

void diag_stage(const char *name) {
    snprintf(s_stage, sizeof(s_stage), "%s", name);
    diag_log("[stage] %s", name);
}

static void add_row(const char *name, const char *detail, int state) {
    if (s_row_count >= DIAG_MAX_ROWS) return;
    DiagRow *r = &s_rows[s_row_count++];
    snprintf(r->name, sizeof(r->name), "%s", name ? name : "?");
    snprintf(r->detail, sizeof(r->detail), "%s", detail ? detail : "");
    r->state = (unsigned char)state;
    if (state == DIAG_FAIL) s_fail_count++;
    if (state == DIAG_WARN) s_warn_count++;
}

void diag_check(const char *name, int ok, const char *detail) {
    add_row(name, detail, ok ? DIAG_OK : DIAG_FAIL);
    if (ok) diag_log("[ OK ] %s", name);
    else diag_log("[FAIL] %s%s%s", name, detail && detail[0] ? " - " : "",
                  detail ? detail : "");
}

void diag_warn(const char *name, const char *detail) {
    add_row(name, detail, DIAG_WARN);
    diag_log("[WARN] %s%s%s", name, detail && detail[0] ? " - " : "",
             detail ? detail : "");
}

int diag_fail_count(void) { return s_fail_count; }
int diag_warn_count(void) { return s_warn_count; }
int diag_row_count(void) { return s_row_count; }
const char *diag_row_name(int index) {
    return (index >= 0 && index < s_row_count) ? s_rows[index].name : "";
}
const char *diag_row_detail(int index) {
    return (index >= 0 && index < s_row_count) ? s_rows[index].detail : "";
}
int diag_row_state(int index) {
    return (index >= 0 && index < s_row_count) ? s_rows[index].state : DIAG_OK;
}

int diag_tail_count(void) { return s_tail_count; }
const char *diag_tail_line(int index) {
    if (index < 0 || index >= s_tail_count) return "";
    int start = (s_tail_next - s_tail_count + DIAG_TAIL_LINES) % DIAG_TAIL_LINES;
    return s_tail[(start + index) % DIAG_TAIL_LINES];
}

const char *diag_last_stage(void) { return s_stage; }
const char *diag_log_path(void) { return DIAG_LOG; }
const char *diag_crash_path(void) { return DIAG_CRASH; }

/* ---- crash dump --------------------------------------------------------- */

static void crash_write(const char *s) {
    if (s_crash_fd < 0 || !s) return;
    size_t len = strlen(s);
    if (len) (void)write(s_crash_fd, s, len);
}

static void crash_hex32(u32 v) {
    static const char hex[] = "0123456789abcdef";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) buf[2 + i] = hex[(v >> (28 - 4 * i)) & 0xF];
    buf[10] = 0;
    crash_write(buf);
}

static void crash_kv(const char *key, u32 value) {
    crash_write(key);
    crash_write(" ");
    crash_hex32(value);
    crash_write("\n");
}

static void crash_reg(int index, u32 value) {
    char key[4];
    key[0] = 'r';
    if (index >= 10) {
        key[1] = '1';
        key[2] = (char)('0' + (index - 10));
        key[3] = 0;
    } else {
        key[1] = (char)('0' + index);
        key[2] = 0;
    }
    crash_kv(key, value);
}

static void diag_exception_handler(ERRF_ExceptionInfo *excep, CpuRegisters *regs)
    __attribute__((noreturn));

static void diag_exception_handler(ERRF_ExceptionInfo *excep, CpuRegisters *regs) {
    crash_write("\n*** crash ***\n");
    crash_kv("excep_type", (u32)excep->type);
    crash_kv("fsr", excep->fsr);
    crash_kv("far", excep->far);
    crash_kv("fpexc", excep->fpexc);
    crash_kv("fpinst", excep->fpinst);
    crash_kv("fpinst2", excep->fpinst2);
    for (int i = 0; i < 13; i++) crash_reg(i, regs->r[i]);
    crash_kv("sp", regs->sp);
    crash_kv("lr", regs->lr);
    crash_kv("pc", regs->pc);
    crash_kv("cpsr", regs->cpsr);
    crash_write("*** end ***\n");
    if (s_crash_fd >= 0) close(s_crash_fd);
    s_crash_fd = -1;

    ERRF_ExceptionHandler(excep, regs);
    __builtin_unreachable();
}

void diag_init(void) {
    s_start_tick = svcGetSystemTick();
    mkdir("sdmc:/3ds", 0777);
    mkdir(DIAG_DIR, 0777);

    /* Keep the previous session next to the current one.  The first boot has
     * nothing to rotate, so every failure here is expected. */
    int prev_crash = 0;
    FILE *old_crash = fopen(DIAG_CRASH, "rb");
    if (old_crash) {
        char head[512];
        size_t n = fread(head, 1, sizeof(head) - 1, old_crash);
        head[n] = 0;
        prev_crash = strstr(head, "*** crash ***") != NULL;
        fclose(old_crash);
    }
    remove(DIAG_LOG_PREV);
    rename(DIAG_LOG, DIAG_LOG_PREV);
    remove(DIAG_CRASH_PREV);
    rename(DIAG_CRASH, DIAG_CRASH_PREV);

    s_log = fopen(DIAG_LOG, "w");
    if (s_log) setvbuf(s_log, NULL, _IOLBF, 0);
    s_crash_fd = open(DIAG_CRASH, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    diag_log("=== sts3ds boot === build %s %s", __DATE__, __TIME__);
    if (prev_crash) diag_check("上次运行崩溃", 0, "见 crash.prev.txt");
    if (!s_log) diag_log("log: cannot open %s", DIAG_LOG);
    if (s_crash_fd < 0) diag_log("crash: cannot open %s", DIAG_CRASH);
    else {
        crash_write("sts3ds crash log\n");
        crash_write("(no crash in this session if this is the only line)\n");
    }

    threadOnException(diag_exception_handler,
                      s_exc_stack + sizeof(s_exc_stack), &s_exc_data);
    diag_log("exception handler installed");
}

void diag_shutdown(void) {
    diag_log("=== sts3ds exit ===");
    if (s_log) {
        fclose(s_log);
        s_log = NULL;
    }
    if (s_crash_fd >= 0) {
        close(s_crash_fd);
        s_crash_fd = -1;
    }
}
