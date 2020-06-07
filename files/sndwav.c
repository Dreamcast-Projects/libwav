#include "sndwav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kos/thread.h>
#include <dc/sound/stream.h>
#include "libwav.h"

/* Keep track of things from the Driver side */
#define SNDDRV_STATUS_NULL         0x00
#define SNDDRV_STATUS_INITIALIZING 0x01
#define SNDDRV_STATUS_READY        0x02
#define SNDDRV_STATUS_STREAMING    0x03
#define SNDDRV_STATUS_DONE         0x04
#define SNDDRV_STATUS_ERROR        0x05

/* Keep track of things from the Decoder side */
#define SNDDEC_STATUS_NULL         0x00
#define SNDDEC_STATUS_INITIALIZING 0x01
#define SNDDEC_STATUS_READY        0x02
#define SNDDEC_STATUS_STREAMING    0x03
#define SNDDEC_STATUS_PAUSING      0x04
#define SNDDEC_STATUS_PAUSED       0x05
#define SNDDEC_STATUS_STOPPING     0x06
#define SNDDEC_STATUS_STOPPED      0x07
#define SNDDEC_STATUS_RESUMING     0x08
#define SNDDEC_STATUS_DONE         0x09
#define SNDDEC_STATUS_ERROR        0x0A

/* Keep track of the buffer status from both sides */
#define SNDDRV_STATUS_NULL         0x00
#define SNDDRV_STATUS_NEEDBUF      0x01
#define SNDDRV_STATUS_HAVEBUF      0x02
#define SNDDRV_STATUS_BUFEND       0x03

#define STREAM_BUFFER_SIZE 100000//65536+16384

typedef void * (*snddrv_cb)(snd_stream_hnd_t, int, int*); 

typedef struct
{
    unsigned char* drv_ptr;
    unsigned int samples_done;
    snddrv_cb callback;

    int loop;
    int chan; // Mono or Stereo
    volatile short status; // Status of stream
    unsigned int rate; // 44100Hz
    unsigned short vol; // 0-255
    snd_stream_hnd_t shnd; // Stream handler

    unsigned char drv_buf[STREAM_BUFFER_SIZE]; // Buffer
    
    FILE* wave_file;

    const unsigned char* wave_buf;
    unsigned int buf_offset;

    // Offset to the data chunk inside wave_file || wave_buf
    unsigned int data_offset;
    unsigned int data_length;
} snddrv_hnd;

static volatile int sndwav_status = SNDDRV_STATUS_DONE;
static snddrv_hnd streams[SND_STREAM_MAX];

static void* sndwav_thread();
static void* wav_file_callback(snd_stream_hnd_t hnd, int req, int* done);
static void* wav_buf_callback(snd_stream_hnd_t hnd, int req, int* done);
static int get_free_slot();

int wav_init() {
    if (snd_stream_init() < 0)
		return 0;

    // Default values
    for(int i=0;i<SND_STREAM_MAX;i++) {
        streams[i].shnd = SND_STREAM_INVALID;
        streams[i].vol = 240;
        streams[i].status = SNDDEC_STATUS_NULL;
        streams[i].callback = NULL;
    }

    if (thd_create(0, sndwav_thread, NULL) != NULL) {
		sndwav_status = SNDDRV_STATUS_READY;
        return 1;
	}
    else {
        sndwav_status = SNDDRV_STATUS_ERROR;
        return 0;
    }
}

void wav_shutdown() {
    sndwav_status = SNDDRV_STATUS_DONE;
    for(int i=0;i<SND_STREAM_MAX;i++) {
        wav_destroy(i);
    }
}

void wav_destroy(wav_stream_hnd_t hnd) {
    if(streams[hnd].shnd == SND_STREAM_INVALID)
        return;

    snd_stream_stop(streams[hnd].shnd);
    snd_stream_destroy(streams[hnd].shnd);
    streams[hnd].shnd = SND_STREAM_INVALID;
    streams[hnd].vol = 240;
    streams[hnd].status = SNDDEC_STATUS_NULL;
    streams[hnd].callback = NULL;
}

wav_stream_hnd_t wav_create(const char* filename, int loop) {
    wav_stream_hnd_t index;

    if(filename == NULL || (index = get_free_slot()) == SND_STREAM_INVALID)
        return SND_STREAM_INVALID;

    streams[index].wave_file = fopen(filename, "rb");
    if(streams[index].wave_file == NULL) {
        printf("FILE I/O ERROR\n");
        return SND_STREAM_INVALID;
    }

    WavFileInfo info = get_wav_info_file(streams[index].wave_file);
    streams[index].loop = loop;
    streams[index].rate = info.sample_rate;
    streams[index].chan = info.channels;
    streams[index].data_offset = info.data_offset;
    streams[index].data_length = info.data_length;
    streams[index].callback = wav_file_callback;
    streams[index].shnd = snd_stream_alloc(streams[index].callback, SND_STREAM_BUFFER_MAX);
    fseek(streams[index].wave_file, streams[index].data_offset, SEEK_SET);
    streams[index].status = SNDDEC_STATUS_READY;

    return index;
}

wav_stream_hnd_t wav_create_fd(FILE* file, int loop) {
    wav_stream_hnd_t index;

    if(file == NULL || (index = get_free_slot()) == SND_STREAM_INVALID)
        return SND_STREAM_INVALID;

    streams[index].wave_file = file;

    WavFileInfo info = get_wav_info_file(streams[index].wave_file);
    streams[index].loop = loop;
    streams[index].rate = info.sample_rate;
    streams[index].chan = info.channels;
    streams[index].data_offset = info.data_offset;
    streams[index].data_length = info.data_length;
    streams[index].callback = wav_file_callback;
    streams[index].shnd = snd_stream_alloc(streams[index].callback, SND_STREAM_BUFFER_MAX);
    fseek(streams[index].wave_file, streams[index].data_offset, SEEK_SET);
    streams[index].status = SNDDEC_STATUS_READY;

    return index;
}

wav_stream_hnd_t wav_create_buf(const unsigned char* buf, int loop) {
    wav_stream_hnd_t index;

    if(buf == NULL || (index = get_free_slot()) == SND_STREAM_INVALID)
        return SND_STREAM_INVALID;

    WavFileInfo info = get_wav_info_buffer(streams[index].wave_buf);
    streams[index].loop = loop;
    streams[index].rate = info.sample_rate;
    streams[index].chan = info.channels;
    streams[index].data_offset = info.data_offset;
    streams[index].data_length = info.data_length;
    streams[index].callback = wav_buf_callback;
    streams[index].shnd = snd_stream_alloc(streams[index].callback, SND_STREAM_BUFFER_MAX);
    streams[index].wave_buf = buf + info.data_offset;
    streams[index].buf_offset = 0;
    streams[index].status = SNDDEC_STATUS_READY;

    return index;
}

void wav_play(wav_stream_hnd_t hnd) {
    if(streams[hnd].status == SNDDEC_STATUS_STREAMING)
       return;

    streams[hnd].status = SNDDEC_STATUS_RESUMING;
}

void wav_pause(wav_stream_hnd_t hnd) {
    if(streams[hnd].status == SNDDEC_STATUS_READY ||
       streams[hnd].status == SNDDEC_STATUS_PAUSING)
       return;
       
    streams[hnd].status = SNDDEC_STATUS_PAUSING;
}

void wav_stop(wav_stream_hnd_t hnd) {
    if(streams[hnd].status == SNDDEC_STATUS_READY ||
       streams[hnd].status == SNDDEC_STATUS_STOPPING)
       return;
       
    streams[hnd].status = SNDDEC_STATUS_STOPPING;
}

void wav_volume(wav_stream_hnd_t hnd, int vol) {
    if(streams[hnd].shnd == SND_STREAM_INVALID)
        return;

    if(vol > 255)
        vol = 255;

    if(vol < 0)
        vol = 0;

    streams[hnd].vol = vol;
    snd_stream_volume(streams[hnd].shnd, streams[hnd].vol);
}

int wav_isplaying(wav_stream_hnd_t hnd) {
    return streams[hnd].status == SNDDEC_STATUS_STREAMING;
}

static void* sndwav_thread() {
    
    while(sndwav_status != SNDDRV_STATUS_DONE && sndwav_status != SNDDRV_STATUS_ERROR) {
        for(int i=0;i<SND_STREAM_MAX;i++) {
            int wav_status = streams[i].status;
            switch(wav_status)
            {
                case SNDDEC_STATUS_INITIALIZING:
                    streams[i].status = SNDDEC_STATUS_READY;
                    break;
                case SNDDEC_STATUS_READY:
                    //
                    break;
                case SNDDEC_STATUS_RESUMING:
                    snd_stream_start(streams[i].shnd, streams[i].rate, streams[i].chan-1);
                    streams[i].status = SNDDEC_STATUS_STREAMING;
                    break;
                case SNDDEC_STATUS_PAUSING:
                    snd_stream_stop(streams[i].shnd);
                    streams[i].status = SNDDEC_STATUS_READY;
                    break;
                case SNDDEC_STATUS_STOPPING:
                    snd_stream_stop(streams[i].shnd);
                    if(streams[i].wave_file != NULL)
                        fseek(streams[i].wave_file, streams[i].data_offset, SEEK_SET);
                    else
                        streams[i].buf_offset = 0;
                    
                    streams[i].status = SNDDEC_STATUS_READY;
                    break;
                case SNDDEC_STATUS_STREAMING:
                    snd_stream_poll(streams[i].shnd);
                    thd_sleep(50);
                    break;
            }
        }
        fflush(stdout);
    }

    wav_shutdown();

    return NULL;
}

static void *wav_file_callback(snd_stream_hnd_t hnd, int req, int* done) {
    int read = fread(streams[hnd].drv_buf, 1, req, streams[hnd].wave_file);
    if(read != req) {
        fseek(streams[hnd].wave_file, streams[hnd].data_offset, SEEK_SET);
        if(streams[hnd].loop) {
            fread(streams[hnd].drv_buf, 1, req, streams[hnd].wave_file);
        }
        else {
            snd_stream_stop(streams[hnd].shnd);
            streams[hnd].status = SNDDEC_STATUS_READY;
            return NULL;
        }
    }

    streams[hnd].drv_ptr = streams[hnd].drv_buf;
    *done = req;
    fflush(stdout);

    return streams[hnd].drv_ptr;
}

static void *wav_buf_callback(snd_stream_hnd_t hnd, int req, int* done) {
    if((streams[hnd].data_length-streams[hnd].buf_offset) >= req)
        memcpy(streams[hnd].drv_buf, streams[hnd].wave_buf+streams[hnd].buf_offset, req);
    else {
        streams[hnd].buf_offset = 0;
        if(streams[hnd].loop) {
            memcpy(streams[hnd].drv_buf, streams[hnd].wave_buf+streams[hnd].buf_offset, req);
        }
        else {
            snd_stream_stop(streams[hnd].shnd);
            streams[hnd].status = SNDDEC_STATUS_READY;
            return NULL;
        }
    }

    streams[hnd].drv_ptr = streams[hnd].drv_buf;  // Need to set drv ptr to drv buffer
    streams[hnd].buf_offset += *done = req;       // Callback returns what was requested

    return streams[hnd].drv_ptr;
}

static int get_free_slot() {
    for(int i=0;i<SND_STREAM_MAX;i++) {
        if(streams[i].shnd == SND_STREAM_INVALID)
            return i;
    }

    return SND_STREAM_INVALID;
}
