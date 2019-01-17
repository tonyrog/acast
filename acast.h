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


extern void print_acast(FILE* f, acast_t* acast);

#endif
