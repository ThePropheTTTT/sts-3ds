#include "audio.h"

#include <3ds.h>
#include <3ds/services/csnd.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define AUDIO_RATE 22050
#define AUDIO_SAMPLES (AUDIO_RATE / 3)   /* one third of a second */
#define AUDIO_BYTES (AUDIO_SAMPLES * 2)

enum AudioBackend { AUDIO_NONE, AUDIO_NDSP, AUDIO_CSND };

static enum AudioBackend s_backend;
static int s_act;
static FILE *s_file;
static s16 *s_pcm[2];
static ndspWaveBuf s_wave[2];
static u8 *s_csnd_pcm;
static u32 s_csnd_bytes;
static int s_total_samples;
static int s_refills;
static char s_track[8];
static char s_init_err[64] = "ok";

static const char *const TRACKS[3] = {"act1", "act2", "act3"};

/* ---- one-shot effects --------------------------------------------------- */

#define SFX_CHANNELS 23                 /* NDSP channels 1..23; 0 is music */
#define SFX_MAX_BYTES (600 * 1024)

/* Keep in sync with the AudioSfx enum and with SFX in tools/extract_audio.py.
 * NONE has no file, so the table is one longer than the number of clips. */
static const char *const SFX_FILES[AUDIO_SFX_COUNT] = {
    NULL,
    "ui_click", "card_select", "card_play", "card_reject",
    "attack_fast", "attack_heavy", "hit", "block",
    "buff", "debuff", "end_turn", "battle_start",
    "victory", "defeat", "exhaust", "heal", "gold",
};

static s16 *s_sfx_pcm[AUDIO_SFX_COUNT];
static u32 s_sfx_samples[AUDIO_SFX_COUNT];
static ndspWaveBuf s_sfx_wave[SFX_CHANNELS];
static int s_sfx_loaded;
static int s_sfx_last;
static int s_sfx_played;
static int s_sfx_muted;

static void sfx_free(void) {
    for (int ch = 1; ch <= SFX_CHANNELS; ch++) ndspChnWaveBufClear((u8)ch);
    for (int i = AUDIO_SFX_NONE + 1; i < AUDIO_SFX_COUNT; i++) {
        if (s_sfx_pcm[i]) {
            linearFree(s_sfx_pcm[i]);
            s_sfx_pcm[i] = NULL;
        }
        s_sfx_samples[i] = 0;
    }
    memset(s_sfx_wave, 0, sizeof(s_sfx_wave));
    s_sfx_loaded = 0;
    s_sfx_last = AUDIO_SFX_NONE;
    s_sfx_played = 0;
}

static void sfx_load(void) {
    for (int i = AUDIO_SFX_NONE + 1; i < AUDIO_SFX_COUNT; i++) {
        char path[80];
        snprintf(path, sizeof(path), "sdmc:/3ds/sts3ds/audio/sfx/%s.pcm",
                 SFX_FILES[i]);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        if (fseek(f, 0, SEEK_END) != 0) { fclose(f); continue; }
        long bytes = ftell(f);
        rewind(f);
        if (bytes <= 1 || bytes > SFX_MAX_BYTES) { fclose(f); continue; }
        s16 *pcm = (s16 *)linearAlloc((u32)bytes);
        if (!pcm) { fclose(f); continue; }
        size_t got = fread(pcm, 1, (size_t)bytes, f);
        fclose(f);
        if (got != (size_t)bytes) { linearFree(pcm); continue; }
        DSP_FlushDataCache(pcm, (u32)bytes);
        s_sfx_pcm[i] = pcm;
        s_sfx_samples[i] = (u32)(bytes / 2);
        s_sfx_loaded++;
    }
    memset(s_sfx_wave, 0, sizeof(s_sfx_wave));
    s_sfx_last = AUDIO_SFX_NONE;
    s_sfx_played = 0;
}

void audio_sfx_mute(int muted) {
    s_sfx_muted = muted != 0;
}

void audio_sfx(AudioSfx id) {
    if (s_sfx_muted) return;
    if (s_backend != AUDIO_NDSP) return;
    if (id <= AUDIO_SFX_NONE || id >= AUDIO_SFX_COUNT) return;
    if (!s_sfx_pcm[id]) return;

    int ch = -1;
    for (int i = 1; i <= SFX_CHANNELS; i++) {
        if (!ndspChnIsPlaying((u8)i)) { ch = i; break; }
    }
    if (ch < 0) return;   /* all effect channels are still busy: drop it */

    ndspChnWaveBufClear((u8)ch);
    ndspWaveBuf *wave = &s_sfx_wave[ch - 1];
    memset(wave, 0, sizeof(*wave));
    wave->data_pcm16 = s_sfx_pcm[id];
    wave->nsamples = s_sfx_samples[id];
    wave->looping = false;
    ndspChnWaveBufAdd((u8)ch, wave);
    ndspChnSetPaused((u8)ch, false);
    s_sfx_last = id;
    s_sfx_played++;
}

void audio_sfx_debug(char *out, int cap) {
    if (!out || cap <= 0) return;
    if (s_backend != AUDIO_NDSP) {
        snprintf(out, (size_t)cap, "音效 不可用");
        return;
    }
    int busy = 0;
    for (int i = 1; i <= SFX_CHANNELS; i++) {
        if (ndspChnIsPlaying((u8)i)) busy++;
    }
    const char *last = (s_sfx_last > AUDIO_SFX_NONE && s_sfx_last < AUDIO_SFX_COUNT)
                       ? SFX_FILES[s_sfx_last] : "-";
    snprintf(out, (size_t)cap, "音效 %d/%d 播放 %d 最近 %s 已发 %d",
             s_sfx_loaded, AUDIO_SFX_COUNT - 1, busy, last, s_sfx_played);
}

/* ---- NDSP: stream small buffers from the file --------------------------- */

static int audio_fill(int slot) {
    size_t got = fread(s_pcm[slot], 1, AUDIO_BYTES, s_file);
    if (got == 0) {
        rewind(s_file);   /* whole-track loop */
        got = fread(s_pcm[slot], 1, AUDIO_BYTES, s_file);
    }
    if (got < 2) return 0;
    got &= ~(size_t)1;    /* PCM16 pairs */
    DSP_FlushDataCache(s_pcm[slot], got);
    s_wave[slot].data_pcm16 = s_pcm[slot];
    s_wave[slot].nsamples = (u32)(got / 2);
    s_wave[slot].looping = false;
    return 1;
}

/* ---- common track handling --------------------------------------------- */

static void audio_stop_track(void) {
    if (s_backend == AUDIO_NDSP) {
        ndspChnWaveBufClear(0);
    } else if (s_backend == AUDIO_CSND) {
        CSND_SetPlayState(0, 0);
        CSND_UpdateInfo(true);
    }
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
    if (s_csnd_pcm) {
        linearFree(s_csnd_pcm);
        s_csnd_pcm = NULL;
    }
    memset(s_wave, 0, sizeof(s_wave));
    s_csnd_bytes = 0;
    s_total_samples = 0;
    s_refills = 0;
    s_track[0] = '\0';
}

/* ---- init / exit -------------------------------------------------------- */

void audio_init(void) {
    if (s_backend != AUDIO_NONE) return;

    /* Preferred path: NDSP streams the track in small buffers.  It needs
     * /3ds/dspfirm.cdc on the SD card, which many users do not have. */
    Result rc = ndspInit();
    if (R_SUCCEEDED(rc)) {
        s_pcm[0] = (s16 *)linearAlloc(AUDIO_BYTES);
        s_pcm[1] = (s16 *)linearAlloc(AUDIO_BYTES);
        if (s_pcm[0] && s_pcm[1]) {
            ndspSetOutputMode(NDSP_OUTPUT_STEREO);
            ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
            ndspChnSetRate(0, AUDIO_RATE);
            ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);
            float mix[12];
            memset(mix, 0, sizeof(mix));
            mix[0] = mix[1] = 1.0f;
            ndspChnSetMix(0, mix);
            for (int ch = 1; ch <= SFX_CHANNELS; ch++) {
                ndspChnSetInterp((u8)ch, NDSP_INTERP_LINEAR);
                ndspChnSetRate((u8)ch, AUDIO_RATE);
                ndspChnSetFormat((u8)ch, NDSP_FORMAT_MONO_PCM16);
                ndspChnSetMix((u8)ch, mix);
            }
            memset(s_wave, 0, sizeof(s_wave));
            s_backend = AUDIO_NDSP;
            s_act = 0;
            sfx_load();
            return;
        }
        if (s_pcm[0]) linearFree(s_pcm[0]);
        if (s_pcm[1]) linearFree(s_pcm[1]);
        s_pcm[0] = s_pcm[1] = NULL;
        ndspExit();
    }

    /* Fallback: CSND does not need the DSP firmware.  It cannot loop a
     * streamed file in small pieces, so it keeps the whole decoded track in
     * linear memory and lets the hardware repeat the two halves. */
    rc = csndInit();
    if (R_SUCCEEDED(rc)) {
        s_backend = AUDIO_CSND;
        s_act = 0;
        return;
    }
    snprintf(s_init_err, sizeof(s_init_err), "ndsp/csnd 失败");
}

void audio_exit(void) {
    if (s_backend == AUDIO_NONE) return;
    audio_stop_track();
    if (s_backend == AUDIO_NDSP) {
        sfx_free();
        ndspExit();
        if (s_pcm[0]) linearFree(s_pcm[0]);
        if (s_pcm[1]) linearFree(s_pcm[1]);
        s_pcm[0] = s_pcm[1] = NULL;
    } else {
        csndExit();
    }
    s_backend = AUDIO_NONE;
}

/* ---- track switching ---------------------------------------------------- */

void audio_set_act(int act) {
    if (s_backend == AUDIO_NONE) return;
    if (act < 1) act = 1;
    if (act > 3) act = 3;
    if (act == s_act) return;

    audio_stop_track();
    s_act = act;
    snprintf(s_track, sizeof(s_track), "%s", TRACKS[act - 1]);

    char path[64];
    snprintf(path, sizeof(path), "sdmc:/3ds/sts3ds/audio/%s.pcm", s_track);
    s_file = fopen(path, "rb");
    if (!s_file) return;

    if (fseek(s_file, 0, SEEK_END) != 0) {
        fclose(s_file);
        s_file = NULL;
        return;
    }
    long bytes = ftell(s_file);
    rewind(s_file);
    if (bytes < 2) {
        fclose(s_file);
        s_file = NULL;
        return;
    }
    s_total_samples = (int)(bytes / 2);

    if (s_backend == AUDIO_CSND) {
        s_csnd_pcm = (u8 *)linearAlloc((u32)bytes);
        if (!s_csnd_pcm) {
            fclose(s_file);
            s_file = NULL;
            return;
        }
        size_t got = fread(s_csnd_pcm, 1, (size_t)bytes, s_file);
        fclose(s_file);
        s_file = NULL;
        if (got != (size_t)bytes) {
            linearFree(s_csnd_pcm);
            s_csnd_pcm = NULL;
            return;
        }
        s_csnd_bytes = (u32)bytes;
        CSND_FlushDataCache(s_csnd_pcm, s_csnd_bytes);
        /* data1 == data0 asks CSND to repeat the whole block, not just its
         * second half. */
        Result rc = csndPlaySound(0,
            SOUND_REPEAT | SOUND_FORMAT_16BIT | SOUND_ENABLE | SOUND_LINEAR_INTERP,
            AUDIO_RATE, 1.0f, 0.0f, s_csnd_pcm, s_csnd_pcm, s_csnd_bytes);
        if (R_FAILED(rc)) {
            linearFree(s_csnd_pcm);
            s_csnd_pcm = NULL;
            s_csnd_bytes = 0;
            return;
        }
        return;
    }

    /* NDSP streaming */
    for (int i = 0; i < 2; i++) {
        if (audio_fill(i)) ndspChnWaveBufAdd(0, &s_wave[i]);
    }
    ndspChnSetPaused(0, false);
}

void audio_update(void) {
    if (s_backend != AUDIO_NDSP) return;
    for (int i = 0; i < 2; i++) {
        if (s_wave[i].status == NDSP_WBUF_DONE) {
            if (s_file && audio_fill(i)) {
                ndspChnWaveBufAdd(0, &s_wave[i]);
                s_refills++;
            }
        }
    }
}

void audio_debug(char *out, int cap) {
    if (!out || cap <= 0) return;
    if (s_backend == AUDIO_NONE) {
        snprintf(out, (size_t)cap, "音乐 未初始化 %s", s_init_err);
        return;
    }
    if (!s_track[0] || !s_total_samples) {
        snprintf(out, (size_t)cap, "音乐 %s.pcm 缺失", s_track[0] ? s_track : "act?");
        return;
    }
    int playing = 0;
    if (s_backend == AUDIO_NDSP) {
        playing = ndspChnIsPlaying(0) ? 1 : 0;
    } else {
        u8 state = 0;
        if (R_SUCCEEDED(csndIsPlaying(0, &state))) playing = state != 0;
    }
    int secs = s_total_samples / AUDIO_RATE;
    if (s_backend == AUDIO_NDSP) {
        snprintf(out, (size_t)cap, "音乐 %s %d:%02d %s NDSP r=%d", s_track,
                 secs / 60, secs % 60, playing ? "PLAYING" : "WAIT", s_refills);
    } else {
        snprintf(out, (size_t)cap, "音乐 %s %d:%02d %s CSND", s_track,
                 secs / 60, secs % 60, playing ? "PLAYING" : "WAIT");
    }
}
