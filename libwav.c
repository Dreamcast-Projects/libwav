#include "libwav.h"

#include <string.h>

typedef struct __attribute__((__packed__)) {
    uint8_t hdr1[4];
    int32_t totalsize;
    uint8_t riff_format[4];

    uint8_t hdr2[4];
    int32_t hdrsize;
    int16_t format;
    int16_t channels;
    int32_t sample_rate;
    int32_t byte_per_sec;
    int16_t blocksize;
    int16_t sample_size;

    uint8_t hdr3[4];
    int32_t data_length;
} wavhdr_t;

int wav_get_info_file(file_t file, WavFileInfo *result) {
    wavhdr_t wavhdr;

    memset(result, 0, sizeof(WavFileInfo));

    fs_seek(file, 0, SEEK_SET);
    if(fs_read(file, &wavhdr, sizeof(wavhdr)) != sizeof(wavhdr)) {
        fs_close(file);
        return 0;
    }

    if(strncmp((const char*)wavhdr.hdr1, "RIFF", 4)) {
        fs_close(file);
        return 0;
    }

    if(strncmp((const char*)wavhdr.riff_format, "WAVE", 4)) {
        fs_close(file);
        return 0;
    }

    if(strncmp((const char *)wavhdr.hdr3, "data", 4)) {
        /* File contains meta data that we want to skip.
           Keep reading until we find the "data" header. */
        fs_seek(file, wavhdr.data_length, SEEK_CUR);

        do {
            /* Read the next chunk header */
            fs_read(file, &(wavhdr.hdr3), 4);

            /* Read the chunk size */
            fs_read(file, &(wavhdr.data_length), 4);

            /* Skip the chunk if it's not the "data" chunk. */
            if(strncmp((const char *)wavhdr.hdr3, "data", 4))
                fs_seek(file, wavhdr.data_length, SEEK_CUR);
        } while(strncmp((const char *)wavhdr.hdr3, "data", 4));
    }

    result->format = wavhdr.format;
    result->channels = wavhdr.channels;
    result->sample_rate = wavhdr.sample_rate;
    result->sample_size = wavhdr.sample_size;
    result->data_length = wavhdr.data_length;

    result->data_offset = fs_tell(file);

    return 1;
}

int wav_get_info_cdda_fd(file_t file, WavFileInfo *result) {
    result->format = WAVE_FORMAT_PCM;
    result->channels = 2;
    result->sample_rate = 44100;
    result->sample_size = 16;
    result->data_length = fs_total(file);

    result->data_offset = 0;

    return 1;
}

int wav_get_info_buffer(const uint8_t *buffer, WavFileInfo *result) {
    wavhdr_t wavhdr;
    size_t data_offset = sizeof(wavhdr_t);

    memset(result, 0, sizeof(WavFileInfo));
    memcpy(&wavhdr, buffer, sizeof(wavhdr_t));

    if(strncmp((const char*)wavhdr.hdr1, "RIFF", 4))
        return 0;

    if(strncmp((const char*)wavhdr.riff_format, "WAVE", 4))
        return 0;

    if(strncmp((const char *)wavhdr.hdr3, "data", 4)) {
        /* File contains meta data that we want to skip.
           Keep reading until we find the "data" header. */
        data_offset += wavhdr.data_length;

        do {
            /* Read the next chunk header */
            memcpy(&(wavhdr.hdr3), buffer + data_offset, 4);
            data_offset += 4;

            /* Read the chunk size */
            memcpy(&(wavhdr.data_length), buffer + data_offset, 4);
            data_offset += 4;

            /* Skip the chunk if it's not the "data" chunk. */
            if(strncmp((const char *)wavhdr.hdr3, "data", 4))
                data_offset += wavhdr.data_length;
        } while(strncmp((const char *)wavhdr.hdr3, "data", 4));
    }

    result->format = wavhdr.format;
    result->channels = wavhdr.channels;
    result->sample_rate = wavhdr.sample_rate;
    result->sample_size = wavhdr.sample_size;
    result->data_length = wavhdr.data_length;

    result->data_offset = data_offset;

    return 1;
}
