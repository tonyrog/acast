//
// acast packet header
//
#ifndef __ACAST_H__
#define __ACAST_H__

#include <stdio.h>
#include <stdint.h>

// default values
#define MULTICAST_ADDR "224.0.0.2"
#define MULTICAST_IF   "0.0.0.0"
#define MULTICAST_PORT 22402

#define BYTES_PER_PACKET 1472     // try avoid ip fragmentation


typedef struct _acast_params_t
{
    int32_t  format;   // snd_pcm_format_t (allow UNKNOWN =-1 to be passed)
    uint32_t channels_per_frame;     // channels per frame
    uint32_t bits_per_channel;       // bits per channel
    uint32_t bytes_per_channel;      // bytes per channel
    uint32_t sample_rate;            // sample rate    
} acast_params_t;

typedef struct _acast_t
{
    uint32_t seqno;          // simple sequence number
    uint32_t num_frames;     // number of frames in packet
    acast_params_t param;    // format params
    uint8_t  data[0];        // audio data
} acast_t;

extern void acast_clear_param(acast_params_t* acast);
extern void acast_print_params(FILE* f, acast_params_t* params);
extern void acast_print(FILE* f, acast_t* acast);

extern int acast_setup_param(snd_pcm_t *handle,
			     acast_params_t* in, acast_params_t* out,
			     snd_pcm_uframes_t* fpp);

// rearrange interleaved channels
// Select channels in src using channel_map
// and put in dst
//
extern int map_channels(snd_pcm_format_t fmt,
			unsigned int src_channels_per_frame,
			void* src,
			unsigned int dst_channels_per_frame,
			void* dst,
			uint8_t* channel_map,
			uint32_t frames);

// interleave channel data found in src using channel_map
// channel_map size must be >= dst_channels_per_frame

extern int interleave_channels(snd_pcm_format_t fmt,
			       void** src,
			       unsigned int dst_channels_per_frame,
			       void* dst,
			       uint8_t* channel_map,
			       uint32_t frames);

#endif
