#ifndef __MP3_H__
#define __MP3_H__

#include <stdio.h>
#include <stdint.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#include <lame/lame.h>

static inline uint32_t mp3_get_bytes_per_frame(mp3data_struct* mp3)
{
    return mp3->stereo*2;  // 2 byte per channel 
}

static inline snd_pcm_uframes_t mp3_get_frames_per_buffer(mp3data_struct* mp3,
							  size_t buffer_size)
{
    return buffer_size / mp3_get_bytes_per_frame(mp3);
}

extern int mp3_decode_init(int fd, hip_t hip, mp3data_struct* mp,
			   int* enc_delay, int* enc_padding);

extern int mp3_decode(int fd, hip_t hip, int16_t* pcm_l, int16_t* pcm_r,
		      mp3data_struct *mp);

extern void mp3_print(FILE* f, mp3data_struct* ptr);

#endif
