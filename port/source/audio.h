/* Streamed background music and combat sound effects.
 *
 * The 3DS has no Vorbis decoder in libctru, so the audio is pre-converted on
 * the host to raw 22050 Hz mono PCM16 (tools/extract_audio.py).  build.sh
 * packs it into the 3dsx romfs when assets/audio exists; the SD card copy at
 * /3ds/sts3ds/audio is an optional fallback.  The music is read in small
 * buffers; the short effects are kept in linear memory and played on NDSP
 * channels 1..23.  NDSP needs the console's DSP firmware;
 * without it a CSND fallback plays the music and the effects stay silent.
 * If the files are missing the game simply stays silent.
 */
#pragma once

/* Start NDSP (or the CSND fallback).  Safe to call once. */
void audio_init(void);
/* Stop every channel, free the buffers and shut the backend down. */
void audio_exit(void);
/* Switch to act1.pcm/act2.pcm/act3.pcm when the act changes. */
void audio_set_act(int act);
/* Refill and requeue any finished music buffer; call once per frame. */
void audio_update(void);
/* One-line music state for the CARD TEST report. */
void audio_debug(char *out, int cap);

/* Keep in sync with SFX in tools/extract_audio.py. */
typedef enum AudioSfx {
    AUDIO_SFX_NONE = 0,
    AUDIO_SFX_UI_CLICK,
    AUDIO_SFX_CARD_SELECT,
    AUDIO_SFX_CARD_PLAY,
    AUDIO_SFX_CARD_REJECT,
    AUDIO_SFX_ATTACK_FAST,
    AUDIO_SFX_ATTACK_HEAVY,
    AUDIO_SFX_HIT,
    AUDIO_SFX_BLOCK,
    AUDIO_SFX_BUFF,
    AUDIO_SFX_DEBUFF,
    AUDIO_SFX_END_TURN,
    AUDIO_SFX_BATTLE_START,
    AUDIO_SFX_VICTORY,
    AUDIO_SFX_DEFEAT,
    AUDIO_SFX_EXHAUST,
    AUDIO_SFX_HEAL,
    AUDIO_SFX_GOLD,
    AUDIO_SFX_COUNT
} AudioSfx;

/* Play a whole clip once.  A no-op until audio_init succeeds and the clip is
 * present; if all effect channels are busy the request is dropped. */
void audio_sfx(AudioSfx id);
/* CARD TEST: silence the hundreds of effect calls the assertions make. */
void audio_sfx_mute(int muted);
/* One-line effect state (loaded clips, busy channels, last clip). */
void audio_sfx_debug(char *out, int cap);
