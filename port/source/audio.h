/* Streamed background music.
 *
 * The 3DS has no Vorbis decoder in libctru, so the music is pre-converted on
 * the host to raw 22050 Hz mono PCM16 (tools/extract_audio.py).  The files live
 * on the SD card and are read in small buffers.  NDSP needs the console's
 * DSP firmware; without it a CSND fallback keeps the whole track in linear
 * memory.  If the files are missing the game simply stays silent.
 */
#pragma once

/* Starts NDSP (or the CSND fallback).  Safe to call once. */
void audio_init(void);
/* Stops the channel, frees the buffers and shuts the backend down. */
void audio_exit(void);
/* Switches to act1.pcm/act2.pcm/act3.pcm when the act changes. */
void audio_set_act(int act);
/* Refills and requeues any finished buffer; call once per frame. */
void audio_update(void);
/* One-line state for the CARD TEST report. */
void audio_debug(char *out, int cap);
