/*
  VitaShell for Switch 2
  Copyright (C) 2026, VitaShell Switch 2 port contributors

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * Switch 2 replacement for audio/vita_audio.c's channel interface
 * (vitaAudioInit, vitaAudioSetChannelCallback, vitaAudioSetFrequency,
 * vitaAudioSetVolume, vitaAudioShutdown) over SDL 2 audio, which reaches
 * the Switch 2 audio output through aud:u.
 *
 * The Vita version ran a thread per channel that asked the player's
 * callback for 1024 stereo 16-bit frames and blocked on the audio port.
 * Here SDL's audio thread pulls those 1024-frame blocks from the callback.
 * The device opens at the first playback (not at startup) and reopens when
 * the stream's sample rate changes; SDL converts to the output's rate.
 * vitaWav* (sound effects) is not used by VitaShell and is not provided.
 */

#include "../../audio/vita_audio.h"

#include <SDL.h>

#include <stdio.h>
#include <string.h>

#define DEFAULT_FREQUENCY 44100

typedef struct {
  vitaAudioCallback callback;
  void *data;
  int volume_left;
  int volume_right;
} AudioChannel;

static AudioChannel channels[VITA_NUM_AUDIO_CHANNELS];

static SDL_AudioDeviceID device = 0;
static int device_frequency = 0;
static int audio_initialized = 0;

/* One block from the callback, consumed by SDL in pieces of any size. */
static short block[VITA_NUM_AUDIO_SAMPLES * 2];
static unsigned int block_position = VITA_NUM_AUDIO_SAMPLES;

static short applyVolume(short sample, int volume) {
  int scaled = ((int)sample * volume) / VITA_VOLUME_MAX;
  if (scaled > 32767)
    return 32767;
  if (scaled < -32768)
    return -32768;
  return (short)scaled;
}

static void refillBlock(void) {
  AudioChannel *channel = &channels[0];

  if (channel->callback) {
    channel->callback(block, VITA_NUM_AUDIO_SAMPLES, channel->data);

    int i;
    for (i = 0; i < VITA_NUM_AUDIO_SAMPLES; i++) {
      block[2 * i] = applyVolume(block[2 * i], channel->volume_left);
      block[2 * i + 1] = applyVolume(block[2 * i + 1], channel->volume_right);
    }
  } else {
    memset(block, 0, sizeof(block));
  }

  block_position = 0;
}

static void SDLCALL deviceCallback(void *userdata, Uint8 *stream, int length) {
  (void)userdata;

  short *out = (short *)stream;
  unsigned int frames = (unsigned int)length / (2 * sizeof(short));

  while (frames > 0) {
    if (block_position >= VITA_NUM_AUDIO_SAMPLES)
      refillBlock();

    unsigned int available = VITA_NUM_AUDIO_SAMPLES - block_position;
    unsigned int count = frames < available ? frames : available;
    memcpy(out, &block[2 * block_position], count * 2 * sizeof(short));

    out += count * 2;
    block_position += count;
    frames -= count;
  }
}

static void closeDevice(void) {
  if (device) {
    SDL_CloseAudioDevice(device);
    device = 0;
    device_frequency = 0;
  }
}

static int openDevice(int frequency) {
  if (!audio_initialized)
    return -1;

  if (device && device_frequency == frequency)
    return 0;

  closeDevice();

  SDL_AudioSpec wanted;
  SDL_zero(wanted);
  wanted.freq = frequency;
  wanted.format = AUDIO_S16SYS;
  wanted.channels = 2;
  wanted.samples = VITA_NUM_AUDIO_SAMPLES;
  wanted.callback = deviceCallback;

  block_position = VITA_NUM_AUDIO_SAMPLES;

  // No allowed changes: SDL converts to whatever the output needs.
  device = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
  if (!device) {
    fprintf(stderr, "audio: open at %d Hz: %s\n", frequency, SDL_GetError());
    return -1;
  }

  device_frequency = frequency;
  SDL_PauseAudioDevice(device, 0);
  return 0;
}

void vitaAudioSetVolume(int channel, int left, int right) {
  if (channel < 0 || channel >= VITA_NUM_AUDIO_CHANNELS)
    return;

  if (device)
    SDL_LockAudioDevice(device);
  channels[channel].volume_left = left;
  channels[channel].volume_right = right;
  if (device)
    SDL_UnlockAudioDevice(device);
}

int vitaAudioSetFrequency(int channel, unsigned short freq) {
  (void)channel;

  // A player that cannot reach the output still shows its file; it plays
  // silence rather than failing to load.
  openDevice(freq);
  return 0;
}

void vitaAudioSetChannelCallback(int channel, vitaAudioCallback callback, void *data) {
  if (channel < 0 || channel >= VITA_NUM_AUDIO_CHANNELS)
    return;

  if (callback && !device)
    openDevice(DEFAULT_FREQUENCY);

  if (device)
    SDL_LockAudioDevice(device);
  channels[channel].callback = callback;
  if (callback)
    channels[channel].data = data;
  if (device)
    SDL_UnlockAudioDevice(device);
}

int vitaAudioInit(int priority) {
  (void)priority;

  int i;
  for (i = 0; i < VITA_NUM_AUDIO_CHANNELS; i++) {
    channels[i].callback = NULL;
    channels[i].data = NULL;
    channels[i].volume_left = VITA_VOLUME_MAX;
    channels[i].volume_right = VITA_VOLUME_MAX;
  }

  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    fprintf(stderr, "audio: SDL audio: %s\n", SDL_GetError());
    return 0;
  }

  audio_initialized = 1;
  return 1;
}

void vitaAudioShutdown(void) {
  closeDevice();

  if (audio_initialized) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audio_initialized = 0;
  }
}
