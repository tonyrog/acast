#ifndef __WAV_H__
#define __WAV_H__

#include <stdint.h>

/* AIFF Definitions */

#define IFF_ID_FORM ((uint32_t)0x464f524d) /* "FORM" */
#define IFF_ID_AIFF ((uint32_t)0x41494646) /* "AIFF" */
#define IFF_ID_AIFC ((uint32_t)0x41494643) /* "AIFC" */
#define IFF_ID_COMM ((uint32_t)0x434f4d4d) /* "COMM" */
#define IFF_ID_SSND ((uint32_t)0x53534e44) /* "SSND" */
#define IFF_ID_MPEG ((uint32_t)0x4d504547) /* "MPEG" */
#define IFF_ID_NONE ((uint32_t)0x4e4f4e45) /* "NONE" *//* AIFF-C data format */
#define IFF_ID_2CBE ((uint32_t)0x74776f73) /* "twos" *//* AIFF-C data format */
#define IFF_ID_2CLE ((uint32_t)0x736f7774) /* "sowt" *//* AIFF-C data format */
#define WAV_ID_RIFF ((uint32_t)0x52494646) /* "RIFF" */
#define WAV_ID_WAVE ((uint32_t)0x57415645) /* "WAVE" */
#define WAV_ID_FMT  ((uint32_t)0x666d7420)  /* "fmt " */
#define WAV_ID_DATA ((uint32_t)0x64617461) /* "data" */

#define WAVE_FORMAT_PCM        ((uint16_t)0x0001)
#define WAVE_FORMAT_IEEE_FLOAT ((uint16_t)0x0003)
#define WAVE_FORMAT_ALAW       ((uint16_t)0x0006)
#define WAVE_FORMAT_ULAW       ((uint16_t)0x0007)
#define WAVE_FORMAT_EXTENSIBLE ((uint16_t)0xFFFE)

typedef struct
{
    uint16_t AudioFormat;
    uint16_t NumChannels;
    uint32_t SampleRate;
    uint32_t ByteRate;
    uint16_t BlockAlign;
    uint16_t BitsPerChannel;
} wav_header_t;

typedef struct
{
    uint16_t cbSize;
    uint16_t ValidBitsPerChannel;
    uint32_t ChannelMask;
    uint16_t AudioFormat;    
} xwav_header_t;

static inline uint32_t wav_get_bytes_per_frame(wav_header_t* hdr)
{
    return hdr->NumChannels*((hdr->BitsPerChannel+7)/8);
}

static inline snd_pcm_uframes_t wav_get_frames_per_buffer(wav_header_t* hdr,
							  size_t buffer_size)
{
    return buffer_size / wav_get_bytes_per_frame(hdr);
}

#endif
