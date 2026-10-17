/*
 * switch_nx.c -- Nintendo Switch (Horizon / libnx) boot glue.
 *
 *   log       nxlink to the PC when launched with `nxlink -s`, otherwise
 *             sdmc:/switch/<MC3_SD_DIR>/<MC3_APP>_log.txt
 *   settings  sdmc:/switch/<MC3_SD_DIR>/<MC3_APP>_env.txt, KEY=VALUE per line,
 *             '#' starts a comment; applied with setenv before boot, so every
 *             RECOMP_* / MC3_* switch works on the console without a rebuild
 *   crashes   CPU exception handler: host PC as an ELF offset (for
 *             aarch64-none-elf-addr2line) plus the guest state
 *   errors    switch_error_dialog(): the system error applet
 *
 * Kept apart from main.c because <switch.h> and the runtime's Win32 vocabulary
 * collide. Only built for the Switch.
 *
 * Known next step (nfsu2-sw measured it): the SD log is the boot bottleneck
 * once the runtime gets chatty -- each flush is a card write. A RAM log with
 * a writer thread fixes it; do that when boot time matters.
 */
#include <switch.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef MC3_APP
#  define MC3_APP "mcdub3"
#endif
#ifndef MC3_SD_DIR
#  define MC3_SD_DIR "mcdub3-nx"
#endif
#define APP_DIR "sdmc:/switch/" MC3_SD_DIR

void print_guest_state(void);   /* main.c */
extern char __start__[];        /* libnx: the ELF's load address */

static int s_nxlink_fd = -1;
static int s_sockets;

/* ------------------------------------------------------------------ */
/* Log                                                                 */
/* ------------------------------------------------------------------ */

static void log_open(void)
{
    if (R_SUCCEEDED(socketInitializeDefault())) {
        s_sockets = 1;
        s_nxlink_fd = nxlinkStdio();       /* -1 unless launched by nxlink */
    }
    if (s_nxlink_fd >= 0)
        return;

    mkdir(APP_DIR, 0777);
    if (!freopen(APP_DIR "/" MC3_APP "_log.txt", "w", stdout))
        return;
    dup2(fileno(stdout), fileno(stderr));
    /* Line-buffered: a crash loses at most the line being written. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
}

/* ------------------------------------------------------------------ */
/* Settings file                                                       */
/* ------------------------------------------------------------------ */

static void env_file_apply(void)
{
    char line[512];
    FILE *f = fopen(APP_DIR "/" MC3_APP "_env.txt", "r");

    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        char *key = line, *eq, *end;

        while (*key == ' ' || *key == '\t')
            key++;
        if (*key == '#' || *key == '\n' || *key == '\r' || !*key)
            continue;
        if (!(eq = strchr(key, '=')))
            continue;
        *eq = '\0';
        end = eq + 1 + strcspn(eq + 1, "\r\n");
        *end = '\0';
        setenv(key, eq + 1, 1);
        printf("[switch] %s=%s\n", key, eq + 1);
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* CPU exceptions                                                      */
/* ------------------------------------------------------------------ */

/* libnx runs __libnx_exception_handler on this stack when a thread faults
 * (requires both symbols). Unaligned atomics (0x103) are the classic
 * x86-vs-AArch64 fault only real hardware shows; emulators don't model it. */
alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump *ctx)
{
    uintptr_t base = (uintptr_t)__start__;

    fprintf(stderr, "[CRASH] exception 0x%X at pc=0x%llX (elf +0x%llX), "
                    "far=0x%llX, lr elf +0x%llX\n",
            ctx->error_desc,
            (unsigned long long)ctx->pc.x,
            (unsigned long long)(ctx->pc.x - base),
            (unsigned long long)ctx->far.x,
            (unsigned long long)(ctx->lr.x - base));
    fprintf(stderr, "  aarch64-none-elf-addr2line -f -e work/elf/mcdub3-<build>.elf 0x%llX\n",
            (unsigned long long)(ctx->pc.x - base));
    print_guest_state();
    log_flush_crash();
    svcExitProcess();
}

/* ------------------------------------------------------------------ */
/* Entry points used by main.c                                         */
/* ------------------------------------------------------------------ */

void switch_error_dialog(const char *msg)
{
    ErrorApplicationConfig c;

    if (R_SUCCEEDED(errorApplicationCreate(&c, msg, NULL)))
        errorApplicationShow(&c);
}

void switch_boot(void)
{
    log_open();
    printf("[switch] " MC3_APP " starting (%s)\n",
           appletGetAppletType() == AppletType_Application
               ? "title takeover" : "applet mode: expect too little memory");
    env_file_apply();
    /* Keep the console awake through long loads and movies. */
    appletSetMediaPlaybackState(true);
}

void switch_shutdown(void)
{
    appletSetMediaPlaybackState(false);
    fflush(stdout);
    fflush(stderr);
    if (s_sockets)
        socketExit();
}
