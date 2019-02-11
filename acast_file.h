//
//  General file interface for audio files
//

#ifndef __ACAST_FILE_H__
#define __ACAST_FILE_H__

#include "acast.h"

typedef struct _acast_file_t
{
    int fd;
    void* private;
    size_t num_frames;  // number of frames in file 
    int (*read)(struct _acast_file_t* af,
		acast_buffer_t* abuf,
		void* buffer, size_t bufsize,
		size_t num_frames);
    int (*write)(struct _acast_file_t* af,
		 acast_buffer_t* abuf,
		 size_t num_frames);
    int (*close)(struct _acast_file_t* af);
} acast_file_t;

extern acast_file_t* acast_file_open(char* filename, int mode);
extern void acast_file_close(acast_file_t* af);

extern int acast_file_read(acast_file_t* af,
			   acast_buffer_t* abuf,
			   void* buffer, size_t bufsize,
			   size_t num_frames);
extern int acast_file_write(acast_file_t* af,
			   acast_buffer_t* abuf,
			   size_t num_frames);

extern snd_pcm_uframes_t acast_file_frames_buffer(acast_file_t* af,
						  size_t bufsize);

// possible backends 
acast_file_t* wav_file_open(char* filename, int mode);
acast_file_t* mp3_file_open(char* filename, int mode);

#endif
