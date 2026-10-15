/*
 * bink_native.c -- Bink 1 movie frames decoded by FFmpeg instead of the
 * lifted Bink decoder.
 *
 * Why: the lifted decoder is x86 translated to C, without its SIMD; on the VM
 * the movies ran at ~22 of their 29.97 fps, and the audio stream starved (9%
 * silent 10 ms blocks) because one game thread decodes video and audio.
 * FFmpeg's binkvideo is bit-exact with Bink's own decoder: the planes the
 * game decoded for mc3intro.bik frame 151 equal FFmpeg's, byte for byte
 * (2026-10-15, make difftest), once V and U are swapped -- Bink stores Y, V, U.
 *
 * How: only the decode step is replaced (src/recomp_manual.c, sub_0037CCD0).
 * Bink keeps doing everything else -- file reading, timing, audio, the YUV to
 * RGB copy, frame bookkeeping. FFmpeg opens the same .bik itself (libavformat)
 * when the game calls BinkOpen, and decodes the frame Bink is on; a jump in
 * frame number (a loop, a skip) is a seek.
 *
 * Host-side only: the wrappers in recomp_manual.c turn guest arguments into
 * host values. Built when CMake finds FFmpeg (MC3_NATIVE_BINK); otherwise the
 * stubs at the bottom keep the lifted decoder in charge. MC3_NATIVE_BINK=0 at
 * run time does the same.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int  mc3_bink_native_enabled(void);
void mc3_bink_native_open(uint32_t hbink, const char *xbox_name);
int  mc3_bink_native_frame(uint32_t hbink, int frame, uint8_t *dest, int w, int h);
void mc3_bink_native_close(uint32_t hbink);

#if defined(MC3_NATIVE_BINK)

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>

/* The runtime's Xbox -> host path translation (xboxrecomp kernel_path.c). */
extern int xbox_translate_path(const char *xbox_path, char *host, unsigned int size);

#define MAX_MOVIES 4

typedef struct {
    uint32_t         hbink;     /* guest HBINK, 0 = free slot */
    AVFormatContext *fmt;
    AVCodecContext  *dec;
    AVPacket        *pkt;
    AVFrame         *frame;
    int              stream;
    int              next;      /* frame index FFmpeg will produce next, -1 unknown */
    long             served, fallbacks;
} Movie;

static Movie s_movies[MAX_MOVIES];

int mc3_bink_native_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_NATIVE_BINK");
        on = !(e && e[0] == '0');
        fprintf(stderr, "[BINK] native (FFmpeg) video decode %s\n", on ? "on" : "off (MC3_NATIVE_BINK=0)");
    }
    return on;
}

static Movie *find(uint32_t hbink)
{
    int i;
    for (i = 0; i < MAX_MOVIES; i++)
        if (s_movies[i].hbink == hbink && hbink)
            return &s_movies[i];
    return NULL;
}

static void release(Movie *m)
{
    if (m->frame) av_frame_free(&m->frame);
    if (m->pkt) av_packet_free(&m->pkt);
    if (m->dec) avcodec_free_context(&m->dec);
    if (m->fmt) avformat_close_input(&m->fmt);
    memset(m, 0, sizeof *m);
}

/* The game passes Xbox paths ("d:\video\x.bik", or relative to the title's
 * directory). Ask the runtime first, then fall back to the game folder. */
static int host_path(const char *xbox_name, char *out, size_t n)
{
    const char *dir;
    size_t i;

    if (xbox_translate_path(xbox_name, out, (unsigned int)n))
        return 1;
    dir = getenv("MC3_GAME_DIR");
    if (!dir || !*dir)
        return 0;
    if (xbox_name[0] && xbox_name[1] == ':')
        xbox_name += 2;
    while (*xbox_name == '\\' || *xbox_name == '/')
        xbox_name++;
    snprintf(out, n, "%s/%s", dir, xbox_name);
    for (i = strlen(dir); out[i]; i++)
        if (out[i] == '\\') out[i] = '/';
    return 1;
}

void mc3_bink_native_open(uint32_t hbink, const char *xbox_name)
{
    char path[1024];
    const AVCodec *codec;
    Movie *m = NULL;
    int i;

    if (!hbink || !mc3_bink_native_enabled())
        return;
    for (i = 0; i < MAX_MOVIES && !m; i++)
        if (!s_movies[i].hbink) m = &s_movies[i];
    if (!m) {
        fprintf(stderr, "[BINK] native: no free slot for %s; lifted decoder\n", xbox_name);
        return;
    }
    if (!host_path(xbox_name, path, sizeof path)) {
        fprintf(stderr, "[BINK] native: cannot locate %s; lifted decoder\n", xbox_name);
        return;
    }
    if (avformat_open_input(&m->fmt, path, NULL, NULL) < 0
            || avformat_find_stream_info(m->fmt, NULL) < 0) {
        fprintf(stderr, "[BINK] native: FFmpeg cannot open %s; lifted decoder\n", path);
        release(m);
        return;
    }
    m->stream = av_find_best_stream(m->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    codec = m->stream < 0 ? NULL
          : avcodec_find_decoder(m->fmt->streams[m->stream]->codecpar->codec_id);
    if (!codec) {
        release(m);
        return;
    }
    m->dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m->dec, m->fmt->streams[m->stream]->codecpar);
    m->dec->thread_count = 1;     /* the game thread waits on this anyway */
    if (avcodec_open2(m->dec, codec, NULL) < 0) {
        release(m);
        return;
    }
    m->pkt = av_packet_alloc();
    m->frame = av_frame_alloc();
    m->hbink = hbink;
    m->next = 0;
    fprintf(stderr, "[BINK] native: %s (%dx%d, %s) for HBINK %08X\n", path,
            m->dec->width, m->dec->height, av_get_pix_fmt_name(m->dec->pix_fmt), hbink);
}

void mc3_bink_native_close(uint32_t hbink)
{
    Movie *m = find(hbink);
    if (!m)
        return;
    fprintf(stderr, "[BINK] native: closed HBINK %08X: %ld frames decoded natively, %ld fell back\n",
            hbink, m->served, m->fallbacks);
    release(m);
}

/* Decode until FFmpeg yields frame `want` (0-based); seek if Bink jumped. */
static int decode_to(Movie *m, int want)
{
    if (m->next < 0 || want < m->next || want > m->next + 30) {
        if (av_seek_frame(m->fmt, m->stream, want, AVSEEK_FLAG_BACKWARD) < 0
                && av_seek_frame(m->fmt, m->stream, 0, AVSEEK_FLAG_BACKWARD) < 0)
            return 0;
        avcodec_flush_buffers(m->dec);
        m->next = -1;
    }
    for (;;) {
        int r = av_read_frame(m->fmt, m->pkt);
        if (r < 0)
            return 0;
        if (m->pkt->stream_index != m->stream) {
            av_packet_unref(m->pkt);
            continue;
        }
        r = avcodec_send_packet(m->dec, m->pkt);
        av_packet_unref(m->pkt);
        if (r < 0)
            return 0;
        while ((r = avcodec_receive_frame(m->dec, m->frame)) == 0) {
            int idx = (int)(m->frame->pts != AV_NOPTS_VALUE ? m->frame->pts : m->next);
            m->next = idx + 1;
            if (idx == want)
                return 1;
            if (idx > want)
                return 0;
        }
    }
}

int mc3_bink_native_frame(uint32_t hbink, int want, uint8_t *dest, int w, int h)
{
    Movie *m = find(hbink);
    int cw = (w + 1) / 2, ch = (h + 1) / 2, y;
    const uint8_t *src;

    if (!m)
        return 0;
    if (m->dec->width != w || m->dec->height != h || m->dec->pix_fmt != AV_PIX_FMT_YUV420P
            || !decode_to(m, want)) {
        m->fallbacks++;
        return 0;
    }
    /* Bink's frame buffer: Y (w x h), then V, then U (each cw x ch), packed. */
    for (y = 0; y < h; y++)
        memcpy(dest + (size_t)y * w, m->frame->data[0] + (size_t)y * m->frame->linesize[0], (size_t)w);
    dest += (size_t)w * h;
    for (y = 0, src = m->frame->data[2]; y < ch; y++)
        memcpy(dest + (size_t)y * cw, src + (size_t)y * m->frame->linesize[2], (size_t)cw);
    dest += (size_t)cw * ch;
    for (y = 0, src = m->frame->data[1]; y < ch; y++)
        memcpy(dest + (size_t)y * cw, src + (size_t)y * m->frame->linesize[1], (size_t)cw);
    m->served++;
    return 1;
}

#else  /* built without FFmpeg: the lifted decoder stays in charge */

int  mc3_bink_native_enabled(void) { return 0; }
void mc3_bink_native_open(uint32_t hbink, const char *xbox_name) { (void)hbink; (void)xbox_name; }
int  mc3_bink_native_frame(uint32_t hbink, int frame, uint8_t *dest, int w, int h)
{ (void)hbink; (void)frame; (void)dest; (void)w; (void)h; return 0; }
void mc3_bink_native_close(uint32_t hbink) { (void)hbink; }

#endif
