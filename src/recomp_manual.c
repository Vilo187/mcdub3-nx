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
#include <strings.h>
#include <time.h>

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

/* D3D fence wait, 0x003DF4D0 (D3D section): waits until the GPU-written
 * semaphore at *(device + 0x30) reaches a fence. The NV2A executor writes
 * BACK_END_WRITE_SEMAPHORE_RELEASE values there only once the project names
 * the address (nv2a_pb_set_semaphore_target, xboxrecomp nv2a_backend.h);
 * until then every release is dropped and the wait never ends (2026-10-14
 * run 15: main thread here with PUT == GET). MC3's D3D keeps its device
 * pointer at 0x003ECAE8 (mov esi, [0x3ECAE8] at the top of this function).
 * Name the semaphore on the first wait, then run the original: its kick-off
 * queues new releases, which now land. */
extern void sub_003DF4D0_gen(void);
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);
void sub_003DF4D0(void)
{
    static uint32_t sem_va;

    if (!sem_va) {
        uint32_t device = MEM32(0x003ECAE8u);
        uint32_t sem = device ? MEM32(device + 0x30u) : 0;
        if (sem) {
            sem_va = sem;
            nv2a_pb_set_semaphore_target(sem_va);
            fprintf(stderr, "[D3D] GPU semaphore at 0x%08X (device 0x%08X)\n",
                    sem_va, device);
        }
    }
    sub_003DF4D0_gen();
}

/* ── Differential tests (src/difftest.c, tools/difftest.py) ── */

/* Wrapped so one call can be captured and replayed on real x86 (make difftest
 * FUNC=<addr> CALL=<n>). Unarmed, a wrapper costs one counter and a compare.
 * Movie frames and movie audio decode wrongly (2026-10-15); BinkDoFrame
 * (0x00377020) itself waits on Bink's I/O thread, so capture below it:
 *   0x0037CCD0  Bink video: decodes the frame's planes (4 x sub_0037AF10,
 *               block decoders 0x003798C0.., transforms 0x00380DF0..);
 *               called from BinkDoFrame at 0x0037758A
 *   0x00378B30  Bink audio: decodes the frame's audio (sub_00378640 and the
 *               .text maths at 0x002B9B00 / 0x002B99C0); at 0x0037730D */
extern void mc3_difftest_call(uint32_t func, void (*lifted)(void), long *calls);
extern long mc3_difftest_wanted(void);

/* ── Music: the WMA decoder XACT drives (MC3_WMA_TRACE=1) ──
 *
 * The music (audio/XStreams/mx.xwb, 110 tracks) is WMA, stereo 44.1 kHz;
 * XACT decodes it on the CPU through the WMADEC section's media object and
 * plays the PCM from a looping buffer voice. On 2026-10-16 that voice ran
 * but every sample it read was zero, so the menu song was silent. This logs
 * what the decoder is asked and answers: XACT creates it with
 * sub_003EFE0D (0x003D6868, when the wave's format tag is 2 = WMA); its
 * vtable is at 0x0046EEA8 (constructor sub_003EFD63). Per method: the first
 * calls in full, then every 500th, with stdcall arguments (this, a, b), the
 * HRESULT, and for any argument that looks like an XMEDIAPACKET
 * {pvBuffer, dwMaxSize, pdwCompletedSize, ...} the completed size and the
 * peak 16-bit sample in it. Each is difftest-capable: Process is slot 4,
 * `make difftest FUNC=003EFCE7 CALL=<n>`. */
static int wma_trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_WMA_TRACE");
        on = e && e[0] == '1';
    }
    return on;
}

/* An XMEDIAPACKET's completed size and 16-bit peak, or 0 if `va` is not one. */
static int wma_packet(uint32_t va, uint32_t *done, uint32_t *max, int *peak)
{
    uint32_t buf, size, done_va, n, i;
    if (va < 0x10000u || va >= 0x84000000u)
        return 0;
    buf = MEM32(va); size = MEM32(va + 4); done_va = MEM32(va + 8);
    if (buf < 0x10000u || buf >= 0x84000000u || !size || size > 0x400000u)
        return 0;
    n = done_va && done_va < 0x84000000u ? MEM32(done_va) : size;
    if (n > size) n = size;
    *done = n; *max = size; *peak = 0;
    for (i = 0; i + 1 < n; i += 2) {
        int v = (int16_t)MEM16(buf + i);
        if (v < 0) v = -v;
        if (v > *peak) *peak = v;
    }
    return 1;
}

static void wma_call(uint32_t addr, int method, void (*fn)(void))
{
    static long calls[11], dcalls[11];
    uint32_t a0, a1, a2;
    long n;
    /* Creation is rare and the question is often whether it happens at
     * all: logged (first 4) with or without the trace. */
    if (!wma_trace_on() && (method != 10 || calls[10] >= 4)) {
        mc3_difftest_call(addr, fn, &dcalls[method]);
        return;
    }
    a0 = MEM32(g_esp + 4); a1 = MEM32(g_esp + 8); a2 = MEM32(g_esp + 12);
    n = ++calls[method];
    mc3_difftest_call(addr, fn, &dcalls[method]);
    if (n <= 12 || n % 500 == 0) {
        uint32_t d, m;
        int pk;
        fprintf(stderr, "[WMA] %s #%ld (%08X, %08X, %08X) -> %08X",
                method == 10 ? "create" : "method", n, a0, a1, a2, g_eax);
        if (method < 10)
            fprintf(stderr, " [vtable +0x%02X]", method * 4);
        if (wma_packet(a1, &d, &m, &pk))
            fprintf(stderr, " | a: %u/%u bytes peak %d", d, m, pk);
        if (wma_packet(a2, &d, &m, &pk))
            fprintf(stderr, " | b: %u/%u bytes peak %d", d, m, pk);
        fprintf(stderr, "\n");
    }
}

/* Written out, not by macro: tools/recomp/manual_scan.py reads the text. */
extern void sub_003EFE0D_gen(void);
void sub_003EFE0D(void) { wma_call(0x003EFE0Du, 10, sub_003EFE0D_gen); }   /* create: (out object, ...) */
extern void sub_003EF9BF_gen(void);
void sub_003EF9BF(void) { wma_call(0x003EF9BFu, 2, sub_003EF9BF_gen); }
extern void sub_003EFA01_gen(void);
void sub_003EFA01(void) { wma_call(0x003EFA01u, 3, sub_003EFA01_gen); }
extern void sub_003EFCE7_gen(void);
void sub_003EFCE7(void) { wma_call(0x003EFCE7u, 4, sub_003EFCE7_gen); }
extern void sub_003EF9FC_gen(void);
void sub_003EF9FC(void) { wma_call(0x003EF9FCu, 5, sub_003EF9FC_gen); }
extern void sub_003EFC83_gen(void);
void sub_003EFC83(void) { wma_call(0x003EFC83u, 6, sub_003EFC83_gen); }
extern void sub_003EFABA_gen(void);
void sub_003EFABA(void) { wma_call(0x003EFABAu, 7, sub_003EFABA_gen); }
extern void sub_003EFB20_gen(void);
void sub_003EFB20(void) { wma_call(0x003EFB20u, 8, sub_003EFB20_gen); }
extern void sub_003EF8EF_gen(void);
void sub_003EF8EF(void) { wma_call(0x003EF8EFu, 9, sub_003EF8EF_gen); }

/* ── Music: XACT's API as the game calls it (MC3_XACT_TRACE=1) ──
 *
 * With MC3_WMA_TRACE on, XACT never created a WMA decoder (2026-10-16):
 * the songs fail before decoding, and the in-game music player shuffles
 * through them forever. These are the 37 XACT functions the game calls
 * directly (0x003D1159..0x003D1794; the last ten are jmp thunks). Each
 * logs its first 3 calls, and every call returning a failure HRESULT (bit
 * 31 set; a few may return counts or pointers instead), with ecx and four
 * stack arguments. */
extern void mc3_difftest_call(uint32_t func, void (*lifted)(void), long *calls);

static int xact_trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_XACT_TRACE");
        on = e && e[0] == '1';
    }
    return on;
}

static void xact_call(uint32_t addr, int slot, void (*fn)(void))
{
    static long calls[64], failures, dcalls[64];
    uint32_t c, a0, a1, a2, a3;
    long n;
    /* Every XACT wrapper can also be captured: make difftest FUNC=<addr>. */
    if (!xact_trace_on()) { mc3_difftest_call(addr, fn, &dcalls[slot]); return; }
    c = g_ecx;
    a0 = MEM32(g_esp + 4); a1 = MEM32(g_esp + 8);
    a2 = MEM32(g_esp + 12); a3 = MEM32(g_esp + 16);
    uint32_t desc[4] = { 0, 0, 0, 0 };
    n = ++calls[slot];
    /* sub_003D162D / sub_003D8B33's second argument is a descriptor:
     * flags (bit 2: streamed), then three words (+0xC: the stream's
     * source). Read before the call, while it is the caller's. */
    if ((addr == 0x003D162Du || addr == 0x003D8B33u) && a1 >= 0x10000u && a1 < 0x84000000u) {
        desc[0] = MEM32(a1); desc[1] = MEM32(a1 + 4);
        desc[2] = MEM32(a1 + 8); desc[3] = MEM32(a1 + 12);
    }
    mc3_difftest_call(addr, fn, &dcalls[slot]);
    if (n <= 3 || ((g_eax & 0x80000000u) && failures++ < 400)) {
        fprintf(stderr, "[XACT] sub_%08X #%ld (ecx %08X; %08X %08X %08X %08X) -> %08X%s",
                addr, n, c, a0, a1, a2, a3, g_eax,
                (g_eax & 0x80000000u) ? "  FAILED" : "");
        if (desc[0] | desc[1] | desc[2] | desc[3])
            fprintf(stderr, " desc {%08X %08X %08X %08X}", desc[0], desc[1], desc[2], desc[3]);
        fprintf(stderr, "\n");
    }
}

/* Written out, not by macro: tools/recomp/manual_scan.py reads the text. */
/* XACT's variation choice, 0x003D796F: thiscall (table offset, flags,
 * float), returns the chosen entry or 0. sub_003D7E76 turns 0 into wave
 * index 0xFFFF and sub_003D89F6 into E_FAIL (0x003D8AA5) -- every song in
 * the music player. Pure computation (mode (flags >> 13) & 15, an RNG at
 * sub_003D21D9 / sub_003D21E2): wrapped for difftest, and logged with
 * MC3_XACT_TRACE. The first call is at boot (the menu song). */
extern void sub_003D796F_gen(void);
void sub_003D796F(void)
{
    static long calls, logged;
    uint32_t c = g_ecx, a0 = MEM32(g_esp + 4), a1 = MEM32(g_esp + 8), a2 = MEM32(g_esp + 12);
    uint32_t entry = 0;
    if (c >= 0x10000u && c < 0x84000000u)
        entry = MEM32(MEM32(c + 4) + a0);
    mc3_difftest_call(0x003D796Fu, sub_003D796F_gen, &calls);
    if (xact_trace_on() && logged++ < 40)
        fprintf(stderr, "[XACT] variation sub_003D796F #%ld (ecx %08X; %08X %08X %08X)"
                " table word %08X (mode %u, count %u) -> %08X\n", logged, c, a0, a1, a2,
                entry, (entry >> 13) & 15, entry & 0x1FFF, g_eax);
}

/* XACT sound states (2026-10-17). Starting a sound moves it to state 5
 * (sub_003D7750 -> sub_003D549F(5)); the only refused transition is from
 * state 7, "stopped", and that refusal is the music player's E_FAIL: every
 * song is stopped before it starts. sub_003D5C0F is XACT's stop (state 7 +
 * cleanup), called from nine places. With MC3_XACT_TRACE these log who stops
 * a sound (the return address names the caller) and every move to state 7 or
 * refused transition, with the sound object and its state before. */
extern void sub_003D5C0F_gen(void);
void sub_003D5C0F(void)
{
    static long n;
    uint32_t snd = g_ecx, ret = MEM32(g_esp), flags = MEM32(g_esp + 4);
    uint32_t state = snd >= 0x10000u && snd < 0x84000000u ? MEM16(snd + 0x30) : 0xFFFF;
    if (xact_trace_on() && n++ < 200)
        fprintf(stderr, "[XACT] stop sound %08X (state %u, flags %08X) called from %08X\n",
                snd, state, flags, ret);
    sub_003D5C0F_gen();
}

extern void sub_003D549F_gen(void);
void sub_003D549F(void)
{
    static long n;
    uint32_t snd = g_ecx, ret = MEM32(g_esp), want = MEM32(g_esp + 4);
    uint32_t before = snd >= 0x10000u && snd < 0x84000000u ? MEM16(snd + 0x30) : 0xFFFF;
    sub_003D549F_gen();
    if (xact_trace_on() && (want == 7 || !g_eax) && n++ < 300)
        fprintf(stderr, "[XACT] sound %08X state %u -> %u %s (from %08X)\n",
                snd, before, want, g_eax ? "ok" : "REFUSED", ret);
}

/* One level down, inside sub_003D162D -> sub_003D8B33 (2026-10-16: E_FAIL
 * 403x while the music player shuffled): which step fails. */
extern void sub_003D8B33_gen(void);
void sub_003D8B33(void) { xact_call(0x003D8B33u, 40, sub_003D8B33_gen); }
extern void sub_003D0E04_gen(void);
void sub_003D0E04(void) { xact_call(0x003D0E04u, 41, sub_003D0E04_gen); }   /* streamed (flag 4) */
extern void sub_003D89F6_gen(void);
void sub_003D89F6(void) { xact_call(0x003D89F6u, 42, sub_003D89F6_gen); }
extern void sub_003D7C30_gen(void);
void sub_003D7C30(void) { xact_call(0x003D7C30u, 43, sub_003D7C30_gen); }
extern void sub_003DAB5B_gen(void);
void sub_003DAB5B(void) { xact_call(0x003DAB5Bu, 44, sub_003DAB5B_gen); }
extern void sub_003D1159_gen(void);
void sub_003D1159(void) { xact_call(0x003D1159u, 0, sub_003D1159_gen); }
extern void sub_003D118B_gen(void);
void sub_003D118B(void) { xact_call(0x003D118Bu, 1, sub_003D118B_gen); }
extern void sub_003D1230_gen(void);
void sub_003D1230(void) { xact_call(0x003D1230u, 2, sub_003D1230_gen); }
extern void sub_003D1248_gen(void);
void sub_003D1248(void) { xact_call(0x003D1248u, 3, sub_003D1248_gen); }
extern void sub_003D128E_gen(void);
void sub_003D128E(void) { xact_call(0x003D128Eu, 4, sub_003D128E_gen); }
extern void sub_003D12CD_gen(void);
void sub_003D12CD(void) { xact_call(0x003D12CDu, 5, sub_003D12CD_gen); }
extern void sub_003D1310_gen(void);
void sub_003D1310(void) { xact_call(0x003D1310u, 6, sub_003D1310_gen); }
extern void sub_003D1353_gen(void);
void sub_003D1353(void) { xact_call(0x003D1353u, 7, sub_003D1353_gen); }
extern void sub_003D1392_gen(void);
void sub_003D1392(void) { xact_call(0x003D1392u, 8, sub_003D1392_gen); }
extern void sub_003D13CD_gen(void);
void sub_003D13CD(void) { xact_call(0x003D13CDu, 9, sub_003D13CD_gen); }
extern void sub_003D140C_gen(void);
void sub_003D140C(void) { xact_call(0x003D140Cu, 10, sub_003D140C_gen); }
extern void sub_003D142C_gen(void);
void sub_003D142C(void) { xact_call(0x003D142Cu, 11, sub_003D142C_gen); }
extern void sub_003D146B_gen(void);
void sub_003D146B(void) { xact_call(0x003D146Bu, 12, sub_003D146B_gen); }
extern void sub_003D14A6_gen(void);
void sub_003D14A6(void) { xact_call(0x003D14A6u, 13, sub_003D14A6_gen); }
extern void sub_003D14E1_gen(void);
void sub_003D14E1(void) { xact_call(0x003D14E1u, 14, sub_003D14E1_gen); }
extern void sub_003D1520_gen(void);
void sub_003D1520(void) { xact_call(0x003D1520u, 15, sub_003D1520_gen); }
extern void sub_003D1563_gen(void);
void sub_003D1563(void) { xact_call(0x003D1563u, 16, sub_003D1563_gen); }
extern void sub_003D159B_gen(void);
void sub_003D159B(void) { xact_call(0x003D159Bu, 17, sub_003D159B_gen); }
extern void sub_003D15B3_gen(void);
void sub_003D15B3(void) { xact_call(0x003D15B3u, 18, sub_003D15B3_gen); }
extern void sub_003D15CF_gen(void);
void sub_003D15CF(void) { xact_call(0x003D15CFu, 19, sub_003D15CF_gen); }
extern void sub_003D15FA_gen(void);
void sub_003D15FA(void) { xact_call(0x003D15FAu, 20, sub_003D15FA_gen); }
extern void sub_003D162D_gen(void);
void sub_003D162D(void) { xact_call(0x003D162Du, 21, sub_003D162D_gen); }
extern void sub_003D1660_gen(void);
void sub_003D1660(void) { xact_call(0x003D1660u, 22, sub_003D1660_gen); }
extern void sub_003D1697_gen(void);
void sub_003D1697(void) { xact_call(0x003D1697u, 23, sub_003D1697_gen); }
extern void sub_003D16CE_gen(void);
void sub_003D16CE(void) { xact_call(0x003D16CEu, 24, sub_003D16CE_gen); }
extern void sub_003D1701_gen(void);
void sub_003D1701(void) { xact_call(0x003D1701u, 25, sub_003D1701_gen); }
extern void sub_003D1734_gen(void);
void sub_003D1734(void) { xact_call(0x003D1734u, 26, sub_003D1734_gen); }
extern void sub_003D1767_gen(void);
void sub_003D1767(void) { xact_call(0x003D1767u, 27, sub_003D1767_gen); }
extern void sub_003D176C_gen(void);
void sub_003D176C(void) { xact_call(0x003D176Cu, 28, sub_003D176C_gen); }
extern void sub_003D1771_gen(void);
void sub_003D1771(void) { xact_call(0x003D1771u, 29, sub_003D1771_gen); }
extern void sub_003D1776_gen(void);
void sub_003D1776(void) { xact_call(0x003D1776u, 30, sub_003D1776_gen); }
extern void sub_003D177B_gen(void);
void sub_003D177B(void) { xact_call(0x003D177Bu, 31, sub_003D177B_gen); }
extern void sub_003D1780_gen(void);
void sub_003D1780(void) { xact_call(0x003D1780u, 32, sub_003D1780_gen); }
extern void sub_003D1785_gen(void);
void sub_003D1785(void) { xact_call(0x003D1785u, 33, sub_003D1785_gen); }
extern void sub_003D178A_gen(void);
void sub_003D178A(void) { xact_call(0x003D178Au, 34, sub_003D178A_gen); }
extern void sub_003D178F_gen(void);
void sub_003D178F(void) { xact_call(0x003D178Fu, 35, sub_003D178F_gen); }
extern void sub_003D1794_gen(void);
void sub_003D1794(void) { xact_call(0x003D1794u, 36, sub_003D1794_gen); }

/* ── Movies: where a frame's time goes (MC3_BINK_TRACE=1) ──
 *
 * The movie loop (sub_0032E7F0) per frame: BinkDoFrame, lock the texture,
 * BinkCopyToBuffer, BinkNextFrame, then spin on BinkWait until the next frame
 * is due; its caller draws and presents. Once a second this prints the
 * average milliseconds per frame in each phase -- "rest" is everything from
 * the end of the wait to the next BinkDoFrame (draw, Present, game). */

enum { BT_DO, BT_VIDEO, BT_AUDIO, BT_LOCK, BT_COPY, BT_NEXT, BT_WAIT, BT_REST, BT_N };
static const char *const s_bt_name[BT_N] =
    { "doframe", "video", "audio", "lock", "copy", "next", "wait", "rest" };
static double s_bt_sum[BT_N], s_bt_mark[BT_N], s_bt_report;
static long s_bt_frames, s_bt_polls;
static double s_bt_wait_start;       /* 0 when not in the wait loop */

static double bt_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static int bt_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_BINK_TRACE");
        on = e && e[0] == '1';
    }
    return on;
}

static void bt_report(double now)
{
    char line[256];
    int n = 0, i;
    if (!s_bt_report) { s_bt_report = now; return; }
    if (now - s_bt_report < 1000.0 || !s_bt_frames) return;
    n += snprintf(line + n, sizeof line - n, "[BINKT] %.1f fps, ms/frame:",
                  s_bt_frames * 1000.0 / (now - s_bt_report));
    for (i = 0; i < BT_N; i++)
        n += snprintf(line + n, sizeof line - n, " %s %.1f", s_bt_name[i], s_bt_sum[i] / s_bt_frames);
    snprintf(line + n, sizeof line - n, " (%ld polls/frame)", s_bt_polls / s_bt_frames);
    fprintf(stderr, "%s\n", line);
    memset(s_bt_sum, 0, sizeof s_bt_sum);
    s_bt_frames = s_bt_polls = 0;
    s_bt_report = now;
}

/* Time one lifted call into phase `p`. */
static void bt_time(int p, void (*fn)(void))
{
    double t0;
    if (!bt_on()) { fn(); return; }
    t0 = bt_now();
    fn();
    s_bt_sum[p] += bt_now() - t0;
}

/* BinkDoFrame, 0x00377020: stdcall (hbink). Opens a frame. */
extern void sub_00377020_gen(void);
void sub_00377020(void)
{
    double t0;
    if (!bt_on()) { sub_00377020_gen(); return; }
    t0 = bt_now();
    if (s_bt_mark[BT_REST]) s_bt_sum[BT_REST] += t0 - s_bt_mark[BT_REST];
    bt_report(t0);
    s_bt_frames++;
    sub_00377020_gen();
    s_bt_mark[BT_LOCK] = bt_now();
    s_bt_sum[BT_DO] += s_bt_mark[BT_LOCK] - t0;
}

/* BinkCopyToBuffer, 0x00376FE0: the YUV -> RGB copy into the locked texture. */
extern void sub_00376FE0_gen(void);
void sub_00376FE0(void)
{
    if (bt_on() && s_bt_mark[BT_LOCK]) {
        s_bt_sum[BT_LOCK] += bt_now() - s_bt_mark[BT_LOCK];
        s_bt_mark[BT_LOCK] = 0;
    }
    bt_time(BT_COPY, sub_00376FE0_gen);
}

/* BinkNextFrame, 0x00377630. */
extern void sub_00377630_gen(void);
void sub_00377630(void)
{
    bt_time(BT_NEXT, sub_00377630_gen);
}

/* BinkWait, 0x00376B50: stdcall (hbink), nonzero = too early, call again. */
extern void sub_00376B50_gen(void);
void sub_00376B50(void)
{
    double now;
    if (!bt_on()) { sub_00376B50_gen(); return; }
    now = bt_now();
    if (!s_bt_wait_start) s_bt_wait_start = now;
    s_bt_polls++;
    sub_00376B50_gen();
    if (!g_eax) {
        now = bt_now();
        s_bt_sum[BT_WAIT] += now - s_bt_wait_start;
        s_bt_wait_start = 0;
        s_bt_mark[BT_REST] = now;
    }
}

/* ── Movies: Bink video decoded by FFmpeg (src/bink_native.c) ── */

/* Bink's HBINK, as far as the native path needs it. */
#define BINK_WIDTH      0x00u
#define BINK_HEIGHT     0x04u
#define BINK_FRAMENUM   0x0Cu       /* 1-based frame being decoded */
#define BINK_DIRTY_MAP  0x3A0u      /* changed-block map, 1 byte per 16x16 block;
                                     * HBINK+0xDC points here, the decoder's 5th
                                     * argument */

extern int  mc3_bink_native_enabled(void);
extern void mc3_bink_native_open(uint32_t hbink, const char *xbox_name);
extern int  mc3_bink_native_frame(uint32_t hbink, int frame, uint8_t *dest, int w, int h);
extern void mc3_bink_native_close(uint32_t hbink);
extern void mc3_bink_native_audio(uint32_t hbink, int frame);
extern int  mc3_bink_native_audio_active(void);

/* MC3_SKIP_MOVIES: for unattended runs (headless, recordings) where nobody
 * is there to press Enter. Off by default: in a window, press it yourself.
 * A comma list of file names without the folder ("mc3intro.bik"), or 1 =
 * the boot intro (rockstar, sdlogo, mc3intro). While such a movie is open,
 * pad 1 pulses A + Start (3 frames held, 3 released: fresh presses) -- input
 * like a player's, through the title's own handling, so only what the game
 * lets you skip is skipped: the Rockstar and Rockstar San Diego logos play
 * in full (as on an Xbox), mc3intro is cut short. */
extern void usb_gamepad_set_held(int dev, uint8_t digital, uint8_t analog);
#define PAD_START 0x10u                         /* digital mask */
#define PAD_A     0x01u                         /* analog mask */
static uint32_t s_skip_hbink;                   /* the movie being pressed through */
static unsigned s_skip_frames;

static int movie_skipped(const char *path)
{
    const char *list = getenv("MC3_SKIP_MOVIES"), *base = path, *p;
    size_t n;

    if (!list || !*list || !strcmp(list, "0"))
        return 0;
    if (!strcmp(list, "1"))
        list = "rockstar.bik,sdlogo.bik,mc3intro.bik";
    for (p = path; *p; p++)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    n = strlen(base);
    for (p = list; *p; ) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == n && !strncasecmp(p, base, n))
            return 1;
        p += len + (e ? 1 : 0);
    }
    return 0;
}

/* BinkOpen, 0x003752F0: stdcall (name, flags), returns the HBINK. */
extern void sub_003752F0_gen(void);
void sub_003752F0(void)
{
    char name[260] = "";
    uint32_t name_va = MEM32(g_esp + 4);

    if (name_va)
        strncpy(name, (const char *)XBOX_PTR(name_va), sizeof name - 1);
    if (name[0] && movie_skipped(name)) {
        sub_003752F0_gen();
        s_skip_hbink = g_eax;
        s_skip_frames = 0;
        fprintf(stderr, "[BINK] pressing A+Start through %s (MC3_SKIP_MOVIES)\n", name);
    } else {
        sub_003752F0_gen();
    }
    if (name[0] && g_eax)
        mc3_bink_native_open(g_eax, name);
}

/* BinkClose, 0x003777D0: stdcall (hbink). */
extern void sub_003777D0_gen(void);
void sub_003777D0(void)
{
    if (s_skip_hbink && MEM32(g_esp + 4) == s_skip_hbink) {
        usb_gamepad_set_held(0, 0, 0);
        fprintf(stderr, "[BINK] skipped after %u frames\n", s_skip_frames);
        s_skip_hbink = 0;
    }
    mc3_bink_native_close(MEM32(g_esp + 4));
    sub_003777D0_gen();
}

/* Bink video decode, 0x0037CCD0: stdcall, 14 arguments -- (dest frame,
 * previous frame, 0, 0, changed-block map = HBINK+0x3A0, w, h, padded w, padded h,
 * packet, ...),
 * returns 1. Called from BinkDoFrame at 0x0037758A. The lifted decoder runs
 * when FFmpeg has no frame for this movie, and for difftest captures. */
extern void sub_0037CCD0_gen(void);
static void bink_video_decode(void)
{
    static long calls;
    uint32_t map = MEM32(g_esp + 20);
    uint32_t hbink = map - BINK_DIRTY_MAP;

    if (s_skip_hbink && hbink == s_skip_hbink) {
        int down = (s_skip_frames++ % 6) < 3;
        usb_gamepad_set_held(0, down ? PAD_START : 0, down ? PAD_A : 0);
    }

    if (!mc3_difftest_wanted() && mc3_bink_native_enabled()) {
        int w = (int)MEM32(hbink + BINK_WIDTH), h = (int)MEM32(hbink + BINK_HEIGHT);
        int frame = (int)MEM32(hbink + BINK_FRAMENUM) - 1;
        mc3_bink_native_audio(hbink, frame);
        if (mc3_bink_native_frame(hbink, frame, (uint8_t *)XBOX_PTR(MEM32(g_esp + 4)), w, h)) {
            /* Every block changed, so BinkCopyToBuffer copies the whole frame:
             * it counts the map's leading non-zero bytes against the block
             * total. Same size as BinkDoFrame's own clear (0x0037748F): the
             * 8th and 9th arguments (padded width, height) over 16. */
            uint32_t blocks = (MEM32(g_esp + 32) >> 4) * (MEM32(g_esp + 36) >> 4);
            memset((void *)XBOX_PTR(map), 1, blocks);
            g_eax = 1;
            g_esp += 4 + 0x38;
            return;
        }
    }
    mc3_difftest_call(0x0037CCD0u, sub_0037CCD0_gen, &calls);
}

void sub_0037CCD0(void)
{
    bt_time(BT_VIDEO, bink_video_decode);
}

/* Bink inverse DCT for one 8x8 block, 0x0037F610: (dest, pitch, coeffs) on
 * the stack. In frame 600 its call #59 writes the one wrong block the
 * video difftest found (dest 0x80F3D400, coefficients at 0x00F7FADC). */
extern void sub_0037F610_gen(void);
void sub_0037F610(void)
{
    static long calls;
    mc3_difftest_call(0x0037F610u, sub_0037F610_gen, &calls);
}

extern void sub_00378B30_gen(void);
/* Bink audio decode, 0x00378B30: stdcall (state, &out_ptr, &out_bytes,
 * packet, &packet_pos), called from BinkDoFrame at 0x0037730D, which then
 * copies *out_bytes of PCM from *out_ptr into its DirectSound ring. While
 * FFmpeg plays the movie's sound (src/bink_native.c), that PCM is zeroed:
 * Bink's streaming and clock carry on, on silence. */
static void bink_audio_decode(void)
{
    static long calls;
    uint32_t out_ptr_va = MEM32(g_esp + 8), out_bytes_va = MEM32(g_esp + 12);

    mc3_difftest_call(0x00378B30u, sub_00378B30_gen, &calls);
    if (mc3_bink_native_audio_active() && !mc3_difftest_wanted()) {
        uint32_t out = MEM32(out_ptr_va), bytes = MEM32(out_bytes_va);
        if (out && bytes && bytes <= 0x100000u)
            memset((void *)XBOX_PTR(out), 0, bytes);
    }
}

void sub_00378B30(void)
{
    bt_time(BT_AUDIO, bink_audio_decode);
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
