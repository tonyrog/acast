//
//  General file interface for audio files
//

#ifndef __ACAST_FILE_H__
#define __ACAST_FILE_H__

#include <stdio.h>
#include <stdlib.h>
#include "acast.h"

typedef struct _acast_file_t
{
    int fd;
    void* private;
    size_t num_frames;   // number of frames in file (0=unknown 0xffffffff=inf)
    acast_params_t param; // currrent params
    
    int (*read)(struct _acast_file_t* af,
		acast_buffer_t* abuf,
		void* buffer, size_t bufsize,
		size_t num_frames);
    
    int (*write)(struct _acast_file_t* af,
		 acast_buffer_t* abuf,
		 size_t num_frames);
    
    int (*close)(struct _acast_file_t* af);
    
    void (*print)(struct _acast_file_t* af, FILE* f);
    
} acast_file_t;

extern acast_file_t* acast_file_open(char* filename, int mode);

static inline void acast_file_close(acast_file_t* af)
{
    (af->close)(af);
}

static inline int acast_file_read(acast_file_t* af,
				  acast_buffer_t* abuf,
				  void* buffer, size_t bufsize,
				  size_t num_frames)
{
    return (af->read)(af, abuf, buffer, bufsize, num_frames);
}

static inline int acast_file_write(acast_file_t* af,
				   acast_buffer_t* abuf,
				   size_t num_frames)
{
    return (af->write)(af, abuf, num_frames);
}

static inline void acast_file_print(acast_file_t* af, FILE* f)
{
    (af->print)(af, f);
}

static inline size_t acast_file_info_bytes_per_frame(acast_file_t* af)
{
    return af->param.bytes_per_channel * af->param.channels_per_frame;
}

static inline size_t acast_file_frames_per_buffer(acast_file_t* af,
						  size_t buffer_size)
{
    return buffer_size / acast_file_info_bytes_per_frame(af);
}

// possible backends
extern acast_file_t* acast_file_open(char* filename, int mode);
extern acast_file_t* wav_file_open(char* filename, int mode);
extern acast_file_t* mp3_file_open(char* filename, int mode);

#endif
