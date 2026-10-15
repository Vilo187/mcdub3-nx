/*
 * difftest.c -- snapshot the guest around one lifted function call, so
 * tools/difftest.py can run the *original* x86 code from the same state in
 * an emulator (Unicorn) and report where the lifted C disagrees.
 *
 * A capture file holds the guest CPU registers and two memory windows:
 *   0x00000000 + RAM size     main RAM: code, data, heap, stacks
 *   0x80000000 + 64 MB        the contiguous window (MmAllocateContiguous...:
 *                             Bink's frame buffers, D3D's resources). Not an
 *                             alias of RAM in this runtime, so captured too.
 *
 * Format (little-endian, no padding), "MC3DIFF1":
 *   char magic[8]; u32 version=1; u32 func; u32 call_index;
 *   u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;
 *   u32 fp_top; u32 fp_control_word; f64 fp_stack[8];
 *   u64 mm[8]; u8 xmm[8][16];
 *   u32 region_count; then per region: u32 va, u32 size, size bytes.
 *
 * Used by wrappers in src/recomp_manual.c, armed with MC3_DIFFTEST_CALL=<n>
 * (capture the n-th call). Off by default.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "recomp_types.h"

extern size_t xbox_GetMappedSize(void);   /* RAM bytes (64 MB retail) */

/* recomp_types.h declares the MMX registers only for generated code; they are
 * 8-byte thread-local unions in the runtime, read here as their 64 bits. */
extern RECOMP_TLS uint64_t g_mm0, g_mm1, g_mm2, g_mm3, g_mm4, g_mm5, g_mm6, g_mm7;

#define CONTIG_VA   0x80000000u
#define CONTIG_SIZE (64u * 1024u * 1024u)

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }

static int write_region(FILE *f, uint32_t va, uint32_t size)
{
    put32(f, va);
    put32(f, size);
    return fwrite((const void *)XBOX_PTR(va), 1, size, f) == size ? 0 : -1;
}

/* Which call to capture: MC3_DIFFTEST_CALL (1-based), 0 = never. */
long mc3_difftest_wanted(void)
{
    static long want = -1;
    if (want < 0) {
        const char *e = getenv("MC3_DIFFTEST_CALL");
        want = e ? atol(e) : 0;
    }
    return want;
}

void mc3_difftest_capture(const char *phase, uint32_t func, long call_index)
{
    const char *dir = getenv("MC3_DIFFTEST_DIR");
    char path[512];
    FILE *f;
    uint32_t ram = (uint32_t)xbox_GetMappedSize();
    int i;

    if (!dir || !*dir)
        dir = "difftest";
    mkdir(dir, 0777);
    snprintf(path, sizeof path, "%s/%08X_%ld_%s.bin", dir, func, call_index, phase);
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[DIFFTEST] cannot write %s\n", path);
        return;
    }
    fwrite("MC3DIFF1", 1, 8, f);
    put32(f, 1);
    put32(f, func);
    put32(f, (uint32_t)call_index);
    put32(f, g_eax); put32(f, g_ecx); put32(f, g_edx); put32(f, g_ebx);
    put32(f, g_esp); put32(f, g_ebp); put32(f, g_esi); put32(f, g_edi);
    put32(f, (uint32_t)g_fp_top);
    put32(f, (uint32_t)g_fp_control_word);
    fwrite(g_fp_stack, sizeof(double), 8, f);
    {
        uint64_t mm[8] = { g_mm0, g_mm1, g_mm2, g_mm3, g_mm4, g_mm5, g_mm6, g_mm7 };
        fwrite(mm, 8, 8, f);
    }
    {
        RecompXmm x[8] = { g_xmm0, g_xmm1, g_xmm2, g_xmm3, g_xmm4, g_xmm5, g_xmm6, g_xmm7 };
        for (i = 0; i < 8; i++) fwrite(&x[i], 16, 1, f);
    }
    put32(f, 2);
    if (write_region(f, 0, ram) || write_region(f, CONTIG_VA, CONTIG_SIZE))
        fprintf(stderr, "[DIFFTEST] short write in %s\n", path);
    fclose(f);
    fprintf(stderr, "[DIFFTEST] %s: sub_%08X call #%ld, esp=%08X -> %s\n",
            phase, func, call_index, g_esp, path);
}
