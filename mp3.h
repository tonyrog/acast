#ifndef __MP3_H__
#define __MP3_H__

#include <stdio.h>
#include <stdint.h>

#include <lame/lame.h>

extern int mp3_decode_init(int fd, hip_t hip, mp3data_struct* mp,
			   int* enc_delay, int* enc_padding);

extern int mp3_decode(int fd, hip_t hip, int16_t* pcm_l, int16_t* pcm_r,
		      mp3data_struct *mp);

extern void mp3_print(FILE* f, mp3data_struct* ptr);

#endif
