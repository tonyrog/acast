//
// acast packet header
//
#ifndef __ACAST_H__
#define __ACAST_H__

#include <stdio.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#include "acast_channel.h"

// default values
#define MULTICAST_ADDR  "224.0.0.223"
#define MULTICAST_PORT  22402          // audio data
#define INTERFACE_ADDR  "0.0.0.0"
#define CONTROL_PORT    22403          // control data
#define CONTROL_MAGIC   0x41434143     // "ACAC"

#define BYTES_PER_PACKET 1472     // try avoid ip fragmentation

typedef struct
{
    int8_t   format;   // snd_pcm_format_t (allow UNKNOWN =-1 to be passed)
    uint8_t  channels_per_frame;     // channels per frame
    uint8_t  bits_per_channel;       // bits per channel
    uint8_t  bytes_per_channel;      // bytes per channel
    uint32_t sample_rate;            // sample rate
} acast_params_t;

typedef struct
{
    uint32_t seqno;          // simple sequence number
    uint32_t num_frames;     // number of frames in packet
    acast_params_t param;    // format params
    uint8_t  data[0];        // audio data
} acast_t;

typedef struct
{
    uint32_t magic;          // control magic
    uint32_t mask;           // channels requested
    uint32_t check;          // crc32 (address+port+magic+mask)
} actl_t;


extern void acast_clear_param(acast_params_t* acast);
extern void acast_print_params(FILE* f, acast_params_t* params);
extern void acast_print(FILE* f, acast_t* acast);

extern snd_pcm_uframes_t acast_get_frames_per_packet(acast_params_t* pp);

extern int acast_setup_param(snd_pcm_t *handle,
			     acast_params_t* in, acast_params_t* out,
			     snd_pcm_uframes_t* fpp);

// bytes_per_frame = 0 => single write
extern long acast_play(snd_pcm_t* handle, size_t bytes_per_frame,
		       uint8_t* buf, size_t len);
// bytes_per_frame = 0 => single read
extern long acast_record(snd_pcm_t* handle, size_t bytes_per_frame,
			 uint8_t* buf, size_t len);


extern void scatter_gather_ii(snd_pcm_format_t fmt,
			      void* src, size_t nsrc,
			      void* dst, size_t ndst,
			      acast_op_t* channel_op, size_t num_ops,
			      size_t frames);

extern void permute_ii(snd_pcm_format_t fmt,
		       void* src, size_t nsrc,
		       void* dst, size_t ndst,
		       uint8_t* channel_map,
		       size_t frames);

extern void permute_ni(snd_pcm_format_t fmt,
		       void** src, size_t nsrc,
		       void* dst, size_t ndst,
		       uint8_t* channel_map,
		       size_t frames);

extern void scatter_gather_ni(snd_pcm_format_t fmt,
			      void** src, size_t* src_stride,
			      void* dst, size_t dst_stride,
			      acast_op_t* channel_op, size_t num_ops,
			      size_t frames);


extern int acast_sender_open(char* maddr, char* ifaddr, int mport,
				 int ttl, int loop,
				 struct sockaddr_in* addr, socklen_t* addrlen,
				 size_t bufsize);

extern int acast_receiver_open(char* maddr, char* ifaddr, int mport,
				   struct sockaddr_in* addr, socklen_t* addrlen,
				   size_t bufsize);

extern int acast_usender_open(char* uaddr, char* ifaddr, int port,
			      struct sockaddr_in* addr, socklen_t* addrlen,
			      size_t bufsize);

#endif
