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
 * Movie sound, likewise (2026-10-15): Bink decodes its audio correctly, but
 * the way to the speakers -- Bink's DirectSound ring, the emulated APU voice,
 * the output -- repeated, skipped and slowed it (tools/audio_diff.py: in
 * mc3intro 1567 repeats of 256..768 samples, 304 skips, 1.2 s of holes, 84%
 * speed). So FFmpeg decodes the soundtrack too and plays it on a voice of
 * the runtime's software mixer (apu_mixer_*, mixed at the output's clock),
 * kept in step with the video frame Bink is on; Bink's own decoded audio is
 * zeroed before it reaches DirectSound, so its timing machinery keeps
 * running on silence. MC3_NATIVE_BINK_AUDIO=0 leaves the sound to Bink;
 * MC3_MOVIE_VOLUME=0..100 (default 100).
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
void mc3_bink_native_audio(uint32_t hbink, int frame);
int  mc3_bink_native_audio_active(void);

#if defined(MC3_NATIVE_BINK)

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>

#include "apu/apu.h"            /* the runtime's software mixer */

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
    struct MovieAudio *audio;   /* NULL: Bink plays its own sound */
} Movie;

static Movie s_movies[MAX_MOVIES];

static Movie *find(uint32_t hbink);

/* ── Movie sound ─────────────────────────────────────────────── */

#define RING_SECS   1           /* the voice loops over a ring this long */
#define LEAD_MS     250         /* decoded sound kept ahead of the play position */
#define RESYNC_MS   120         /* off by more than this from the video: correct */

typedef struct MovieAudio {
    AVFormatContext *fmt;       /* its own reader: independent of the video's */
    AVCodecContext  *dec;
    AVPacket        *pkt;
    AVFrame         *frame;
    int              stream, eof;
    int              ch, rate;
    double           fps;       /* video frames per second */
    int16_t         *ring;      /* ring_frames x ch, interleaved s16 */
    uint32_t         ring_frames;
    int              voice, started;
    int              counted;   /* in s_audio_users: Bink's own sound muted */
    uint64_t         written;   /* frames written into the ring (sound or silence) */
    uint64_t         played;    /* frames the mixer has consumed */
    uint32_t         last_pos;
    int64_t          src;       /* soundtrack frames taken from FFmpeg (played or dropped) */
    int16_t         *pend;      /* decoded frames not yet in the ring */
    int              pend_n, pend_off, pend_cap;
    long             dropped, inserted, underruns;
} MovieAudio;

static int s_audio_users;      /* movies whose sound is ours */

int mc3_bink_native_audio_active(void)
{
    return s_audio_users > 0;
}

static int audio_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_NATIVE_BINK_AUDIO");
        on = !(e && e[0] == '0');
    }
    return on;
}

static void audio_free(MovieAudio *a)
{
    if (!a) return;
    if (a->voice >= 0) {
        apu_mixer_stop(a->voice);
        apu_mixer_free_voice(a->voice);
    }
    if (a->counted) s_audio_users--;
    if (a->frame) av_frame_free(&a->frame);
    if (a->pkt) av_packet_free(&a->pkt);
    if (a->dec) avcodec_free_context(&a->dec);
    if (a->fmt) avformat_close_input(&a->fmt);
    free(a->ring);
    free(a->pend);
    free(a);
}

static MovieAudio *audio_open(const char *path, double fps)
{
    MovieAudio *a = calloc(1, sizeof *a);
    const AVCodec *codec;
    const char *e;

    if (!a) return NULL;
    a->voice = -1;
    if (avformat_open_input(&a->fmt, path, NULL, NULL) < 0
            || avformat_find_stream_info(a->fmt, NULL) < 0)
        goto fail;
    a->stream = av_find_best_stream(a->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (a->stream < 0)
        goto fail;              /* a silent movie */
    codec = avcodec_find_decoder(a->fmt->streams[a->stream]->codecpar->codec_id);
    if (!codec || !(a->dec = avcodec_alloc_context3(codec)))
        goto fail;
    avcodec_parameters_to_context(a->dec, a->fmt->streams[a->stream]->codecpar);
    if (avcodec_open2(a->dec, codec, NULL) < 0)
        goto fail;
#if LIBAVCODEC_VERSION_MAJOR >= 60
    a->ch = a->dec->ch_layout.nb_channels;
#else
    a->ch = a->dec->channels;
#endif
    a->rate = a->dec->sample_rate;
    if (a->ch < 1 || a->ch > 2 || a->rate <= 0)
        goto fail;
    a->fps = fps > 0 ? fps : 30000.0 / 1001.0;
    a->ring_frames = (uint32_t)(a->rate * RING_SECS);
    a->ring = calloc(a->ring_frames, (size_t)a->ch * sizeof(int16_t));
    a->pkt = av_packet_alloc();
    a->frame = av_frame_alloc();
    a->voice = apu_mixer_alloc_voice();
    if (!a->ring || !a->pkt || !a->frame || a->voice < 0)
        goto fail;
    {
        APUMixerVoice *v = apu_mixer_get_voice(a->voice);
        v->pcm_data = a->ring;
        v->pcm_bytes = a->ring_frames * (uint32_t)a->ch * (uint32_t)sizeof(int16_t);
        v->num_channels = (uint32_t)a->ch;
        v->sample_rate = (uint32_t)a->rate;
        e = getenv("MC3_MOVIE_VOLUME");
        v->volume = e ? (float)atoi(e) / 100.0f : 1.0f;
    }
    /* Bink's own sound is muted from here on: frame 0's audio is decoded
     * before its video, so waiting for the first video frame is too late. */
    a->counted = 1;
    s_audio_users++;
    return a;
fail:
    audio_free(a);
    return NULL;
}

static int16_t to_s16(float f)
{
    f *= 32768.0f;
    return (int16_t)(f > 32767.0f ? 32767 : f < -32768.0f ? -32768 : (int)f);
}

/* Decode the next audio frame into a->pend (interleaved s16). 0 at the end. */
static int audio_decode(MovieAudio *a)
{
    for (;;) {
        int r = avcodec_receive_frame(a->dec, a->frame);
        if (r == 0) {
            int n = a->frame->nb_samples, ch = a->ch, i, c;
            if (n * ch > a->pend_cap) {
                free(a->pend);
                a->pend_cap = n * ch;
                a->pend = malloc((size_t)a->pend_cap * sizeof(int16_t));
                if (!a->pend) { a->pend_cap = 0; return 0; }
            }
            for (i = 0; i < n; i++)
                for (c = 0; c < ch; c++) {
                    int16_t v = 0;
                    switch (a->frame->format) {
                    case AV_SAMPLE_FMT_FLTP: v = to_s16(((const float *)a->frame->data[c])[i]); break;
                    case AV_SAMPLE_FMT_FLT:  v = to_s16(((const float *)a->frame->data[0])[i * ch + c]); break;
                    case AV_SAMPLE_FMT_S16P: v = ((const int16_t *)a->frame->data[c])[i]; break;
                    case AV_SAMPLE_FMT_S16:  v = ((const int16_t *)a->frame->data[0])[i * ch + c]; break;
                    default: break;
                    }
                    a->pend[i * ch + c] = v;
                }
            a->pend_n = n;
            a->pend_off = 0;
            return 1;
        }
        if (r != AVERROR(EAGAIN) || a->eof)
            return 0;
        if (av_read_frame(a->fmt, a->pkt) < 0) {
            a->eof = 1;
            avcodec_send_packet(a->dec, NULL);
            continue;
        }
        if (a->pkt->stream_index == a->stream)
            avcodec_send_packet(a->dec, a->pkt);
        av_packet_unref(a->pkt);
    }
}

/* Move n frames: the soundtrack's next frames into the ring (src, ring),
 * silence into the ring (!src, ring), or soundtrack frames dropped
 * (src, !ring). Past the soundtrack's end, silence. */
static void audio_take(MovieAudio *a, int64_t n, int src, int ring)
{
    while (n > 0) {
        int64_t k = n;
        const int16_t *from = NULL;

        if (ring) {
            uint32_t room = a->ring_frames - (uint32_t)(a->written % a->ring_frames);
            if (k > room) k = room;
        }
        if (src) {
            if (a->pend_off >= a->pend_n && !audio_decode(a)) {
                src = 0;                        /* the soundtrack ended */
                if (!ring) return;
                continue;
            }
            if (k > a->pend_n - a->pend_off) k = a->pend_n - a->pend_off;
            from = a->pend + (size_t)a->pend_off * a->ch;
            a->pend_off += (int)k;
            a->src += k;
        }
        if (ring) {
            int16_t *to = a->ring + (size_t)(a->written % a->ring_frames) * a->ch;
            if (from)
                memcpy(to, from, (size_t)k * a->ch * sizeof(int16_t));
            else
                memset(to, 0, (size_t)k * a->ch * sizeof(int16_t));
            a->written += (uint64_t)k;
        }
        n -= k;
    }
}

/* Zero the stretch after the write position, so a stall (a loading pause,
 * the movie's end) plays silence rather than the ring's previous lap. */
static void audio_zero_ahead(MovieAudio *a, uint32_t frames)
{
    uint64_t w = a->written;
    while (frames) {
        uint32_t at = (uint32_t)(w % a->ring_frames), k = a->ring_frames - at;
        if (k > frames) k = frames;
        memset(a->ring + (size_t)at * a->ch, 0, (size_t)k * a->ch * sizeof(int16_t));
        w += k;
        frames -= k;
    }
}

/* Once per video frame, from the game thread: account for what the mixer has
 * played, keep the soundtrack in step with video frame `frame`, top up. */
void mc3_bink_native_audio(uint32_t hbink, int frame)
{
    Movie *m = find(hbink);
    MovieAudio *a = m ? m->audio : NULL;
    uint32_t pos, frame_bytes;
    int64_t lead_target, drift, want;

    if (!a) return;
    frame_bytes = (uint32_t)a->ch * (uint32_t)sizeof(int16_t);
    lead_target = (int64_t)a->rate * LEAD_MS / 1000;

    if (a->started) {
        int active = 0;
        apu_mixer_get_state(a->voice, &pos, &active, NULL);
        pos /= frame_bytes;
        a->played += (pos + a->ring_frames - a->last_pos) % a->ring_frames;
        a->last_pos = pos;
        if (a->played > a->written) {           /* ran dry: it played silence */
            a->underruns++;
            a->written = a->played + (uint64_t)(a->rate / 100);
        }
        /* Which soundtrack frame plays now, against the one the video is on. */
        want = (int64_t)((double)frame * a->rate / a->fps);
        drift = (a->src - (int64_t)(a->written - a->played)) - want;
        if (drift < -(int64_t)a->rate * RESYNC_MS / 1000) {
            audio_take(a, -drift, 1, 0);        /* behind: skip ahead */
            a->dropped += -drift;
        } else if (drift > (int64_t)a->rate * RESYNC_MS / 1000) {
            int64_t n = drift < lead_target ? drift : lead_target;
            audio_take(a, n, 0, 1);             /* ahead: hold with silence */
            a->inserted += n;
        }
    } else if (frame > 0) {
        /* Joined late (a seek, or the native path started mid-movie). */
        audio_take(a, (int64_t)((double)frame * a->rate / a->fps), 1, 0);
    }

    if ((int64_t)(a->written - a->played) < lead_target)
        audio_take(a, lead_target - (int64_t)(a->written - a->played), 1, 1);
    audio_zero_ahead(a, a->ring_frames / 4);

    if (!a->started) {
        apu_mixer_set_position(a->voice, 0);
        apu_mixer_play(a->voice, 1);
        a->started = 1;
    }
}

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
    audio_free(m->audio);
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
    if (audio_enabled()) {
        AVRational fr = m->fmt->streams[m->stream]->avg_frame_rate;
        m->audio = audio_open(path, fr.den ? av_q2d(fr) : 0);
        if (m->audio)
            fprintf(stderr, "[BINK] native sound: %d Hz, %d ch, mixer voice %d\n",
                    m->audio->rate, m->audio->ch, m->audio->voice);
    }
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
    if (m->audio)
        fprintf(stderr, "[BINK] native sound: %.1f s played, resyncs dropped %.0f ms / inserted %.0f ms,"
                " %ld underruns\n", (double)m->audio->played / m->audio->rate,
                m->audio->dropped * 1000.0 / m->audio->rate,
                m->audio->inserted * 1000.0 / m->audio->rate, m->audio->underruns);
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
void mc3_bink_native_audio(uint32_t hbink, int frame) { (void)hbink; (void)frame; }
int  mc3_bink_native_audio_active(void) { return 0; }

#endif
