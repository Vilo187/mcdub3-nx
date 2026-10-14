/**
 * Midnight Club 3: DUB Edition -- hand-written overrides of lifted functions
 * and ICALL diagnostics. Started from xboxrecomp's templates/new-game (MIT).
 *
 * tools/regen.sh passes this file to the recompiler (--exclude-manual): any
 * sub_XXXXXXXX defined here is not generated, so the hand-written body links
 * instead. After editing, LIFT_ONLY=1 tools/regen.sh.
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The generated register model (gen/recomp_types.h): the guest registers,
 * MEM32 and friends, and the ICALL trace ring the runtime owns. Include it
 * rather than re-declaring registers by hand: they are thread-local
 * (RECOMP_TLS), and a plain `extern uint32_t g_eax` is a TLS mismatch that
 * GCC refuses to link on Linux and the Switch. */
#include "recomp_types.h"

/* ── Native replacements ───────────────────────────────────── */

/* memmove (CRT), 0x002AB610 -- func_id: memmove, confidence 0.9.
 *
 * The lifted body dispatches its head and tail copies through two jump tables
 * (0x002AB670, 0x002AB76C) whose targets are labels inside the function. The
 * vendored lifter emits those as indirect calls to other functions, which
 * resolve to nothing ([ICALL] Failed to resolve VA 0x002AB8B4, first run): the
 * copy silently stops short. Upstream fixed this class of jump table after our
 * base; until it is merged, the host's memmove does the job (faster, too).
 *
 * cdecl: dest, src, count on the guest stack above the return address;
 * returns dest in eax; the caller pops the arguments, we pop the return
 * address as `ret` would. */
void sub_002AB610(void)
{
    uint32_t dst = MEM32(g_esp + 4);
    uint32_t src = MEM32(g_esp + 8);
    uint32_t len = MEM32(g_esp + 12);

    if (len)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), len);
    g_eax = dst;
    g_esp += 4;
}

/* Bink's MMX detection, 0x002B9C90 (called by the blit set-up sub_0037DB10).
 *
 * The classic test: flip EFLAGS.ID with pushfd/popfd to see whether CPUID
 * exists, then CPUID leaf 1, EDX bit 23 = MMX. The vendored lifter emits
 * pushfd, popfd and cpuid as silent no-ops, so the ID bit never "sticks",
 * Bink decides the CPU has no MMX, never installs its pixel routines
 * (0x5209C8.. stay 0), and the blit loop -- whose stdcall calls through those
 * null pointers are skipped without popping their argument -- drifts its
 * stack and never ends (2026-10-14 runs, [BINKLOOP] trace).
 *
 * The Xbox CPU is a Pentium III: CPUID and MMX are always there. Same
 * results and side effects as the original:
 *   [0x482AA4] = cached "has MMX" (-1 until detected)
 *   [0x52A43C] = "use MMX", unless the caller's flags carry 0x40000
 * cdecl: flags at [esp+4]; returns the detection result in eax. */
void sub_002B9C90(void)
{
    uint32_t flags = MEM32(g_esp + 4);
    uint32_t has_mmx = MEM32(0x482AA4);

    if ((int32_t)has_mmx < 0) {
        has_mmx = 1;
        MEM32(0x482AA4) = has_mmx;
    }
    MEM32(0x52A43C) = (has_mmx && !(flags & 0x40000u)) ? 1u : 0u;
    g_eax = has_mmx;
    g_esp += 4;
}

/* ── Traces (wrap the lifted body, change nothing) ─────────── */

/* Bink 32-bit blit, 0x00382C00 (BINK32): a 12-argument stdcall wrapper
 * (ret 0x30) around the blit driver sub_0037DF90. On the 2026-10-14 runs the
 * driver's row loop spun with no pixel routine ever called -- consistent with
 * a garbage height/width making ((height - start - 1) >> 1) + 1 wrap. Log the
 * arguments of the first calls to see what the movie player hands it.
 * Remove once the movie plays. */
extern void sub_00382C00_gen(void);
void sub_00382C00(void)
{
    static int logged;

    if (logged < 4) {
        int i;
        logged++;
        fprintf(stderr, "[BINK32] blit #%d args:", logged);
        for (i = 1; i <= 12; i++)
            fprintf(stderr, " %08X", MEM32(g_esp + 4u * (uint32_t)i));
        fprintf(stderr, "\n");
    }
    sub_00382C00_gen();
}

/* Bink blit timing, 0x0037DF00: called once per pass of the row loop in
 * sub_0037DF90 (call at 0x0037E311). The loop counter is the caller's
 * [esp+0x40]; from in here the caller's esp is g_esp + 4 (our return
 * address), so the counter is MEM32(g_esp + 0x44). The blit arguments were
 * sane (640x480, pitch 0xA00), yet the loop never ends. Log whether the
 * counter moves, whether esp drifts between passes, and what the per-CPU
 * pixel-routine pointers hold. Remove once the movie plays. */
extern void sub_0037DF00_gen(void);
void sub_0037DF00(void)
{
    static uint64_t calls;

    calls++;
    if (calls <= 3 || calls % 1000000u == 0) {
        fprintf(stderr, "[BINKLOOP] pass %llu: counter=%08X esp=%08X "
                        "ptrs c8=%08X d8=%08X f0=%08X f4=%08X f8=%08X\n",
                (unsigned long long)calls, MEM32(g_esp + 0x44), g_esp,
                MEM32(0x5209C8), MEM32(0x5209D8), MEM32(0x5209F0),
                MEM32(0x5209F4), MEM32(0x5209F8));
    }
    sub_0037DF00_gen();
}

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}
