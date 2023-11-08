#include "libwav.h"

#include <string.h>

typedef struct __attribute__((__packed__)) {
    uint8_t hdr1[4];
    int32_t totalsize;

    uint8_t hdr2[8];
    int32_t hdrsize;
    int16_t format;
    int16_t channels;
    int32_t sample_rate;
    int32_t byte_per_sec;
    int16_t blocksize;
    int16_t sample_size;

    uint8_t hdr3[4];
    int32_t datasize;
} wavhdr_t;

int wav_get_info_file(FILE *file, WavFileInfo *result) {
    wavhdr_t wavhdr;

    memset(result, 0, sizeof(WavFileInfo));

    fseek(file, 0, SEEK_SET);
    if(fread(&wavhdr, sizeof(wavhdr), 1, file) != 1) {
        fclose(file);
        return 0;
    }

    if(strncmp((const char *)wavhdr.hdr3, "data", 4)) {
        /* File contains meta data that we want to skip.
           Keep reading until we find the "data" header. */
        fseek(file, wavhdr.datasize, SEEK_CUR);

        do {
            /* Read the next chunk header */
            fread(&(wavhdr.hdr3), 4, 1, file);

            /* Read the chunk size */
            fread(&(wavhdr.datasize), 4, 1, file);

            /* Skip the chunk if it's not the "data" chunk. */
            if(strncmp((const char *)wavhdr.hdr3, "data", 4))
                fseek(file, wavhdr.datasize, SEEK_CUR);
        } while(strncmp((const char *)wavhdr.hdr3, "data", 4));
    }

    result->format = wavhdr.format;
    result->channels = wavhdr.channels;
    result->sample_rate = wavhdr.sample_rate;
    result->sample_size = wavhdr.sample_size;
    result->data_length = wavhdr.datasize;

    result->data_offset = ftell(file);

    return 1;
}

int wav_get_info_buffer(const unsigned char* buffer, WavFileInfo* result) {
    wavhdr_t wavhdr;
    size_t offset = sizeof(wavhdr_t);

    memset(result, 0, sizeof(WavFileInfo));
    memcpy(&wavhdr, buffer, sizeof(wavhdr_t));

    if(strncmp((const char *)wavhdr.hdr3, "data", 4)) {
        /* File contains meta data that we want to skip.
           Keep reading until we find the "data" header. */
        offset += wavhdr.datasize;

        do {
            /* Read the next chunk header */
            memcpy(&(wavhdr.hdr3), buffer + offset, 4);
            offset += 4;

            /* Read the chunk size */
            memcpy(&(wavhdr.datasize), buffer + offset, 4);
            offset += 4;

            /* Skip the chunk if it's not the "data" chunk. */
            if(strncmp((const char *)wavhdr.hdr3, "data", 4))
                offset += wavhdr.datasize;
        } while(strncmp((const char *)wavhdr.hdr3, "data", 4));
    }

    result->format = wavhdr.format;
    result->channels = wavhdr.channels;
    result->sample_rate = wavhdr.sample_rate;
    result->sample_size = wavhdr.sample_size;
    result->data_length = wavhdr.datasize;

    result->data_offset = offset;

    return 1;
}