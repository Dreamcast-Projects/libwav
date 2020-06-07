#ifndef SNDWAV_H
#define SNDWAV_H

#include <stdio.h>

typedef int wav_stream_hnd_t;
typedef void (*snddrv_filter)(wav_stream_hnd_t hnd, void *obj, int hz, int channels, void **buffer, int *samplecnt);

int wav_init();
void wav_shutdown();
void wav_destroy(wav_stream_hnd_t hnd);

wav_stream_hnd_t wav_create(const char* filename, int loop);
wav_stream_hnd_t wav_create_fd(FILE* f, int loop);
wav_stream_hnd_t wav_create_buf(const unsigned char* buf, int loop);

void wav_play(wav_stream_hnd_t hnd);
void wav_pause(wav_stream_hnd_t hnd);
void wav_stop(wav_stream_hnd_t hnd);
void wav_volume(wav_stream_hnd_t hnd, int vol);
int wav_isplaying(wav_stream_hnd_t hnd);

void wav_add_filter(wav_stream_hnd_t hnd, snddrv_filter filter, void* obj);
void wav_remove_filter(wav_stream_hnd_t hnd, snddrv_filter filter, void* obj);

#endif
