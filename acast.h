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

typedef enum {
    ACAST_OP_SRC1,
    ACAST_OP_CONST1,
    ACAST_OP_ADD,
    ACAST_OP_SUB,
} acast_channel_op_t;

typedef struct
{
    acast_channel_op_t op;
    int src1;
    int src2;
    int dst;
} acast_op_t;

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

// rearrange interleaved channels
// Select channels in src using channel_map
// and put in dst
//
extern void map_channels(snd_pcm_format_t fmt,
			 void* src, unsigned int src_channels_per_frame,
			 void* dst, unsigned int dst_channels_per_frame,
			 uint8_t* channel_map,
			 uint32_t frames);

extern void op_channels(snd_pcm_format_t fmt,
			void* src, unsigned int src_channels_per_frame,
			void* dst, unsigned int dst_channels_per_frame,
			acast_op_t* channel_op, size_t num_ops,
			uint32_t frames);

// source channels are separate result is interleaved
extern void imap_channels(snd_pcm_format_t fmt,
			  void** src,
			  unsigned int dst_channels_per_frame,
			  void* dst,
			  uint8_t* channel_map,
			  uint32_t frames);

extern void iop_channels(snd_pcm_format_t fmt,
			 void** src,
			 unsigned int src_channels_per_frame,
			 void* dst, unsigned int dst_channels_per_frame,
			 acast_op_t* channel_op, size_t num_ops,
			 uint32_t frames);



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

extern void print_channel_ops(acast_op_t* channel_op, size_t num_ops);

extern int build_channel_map(acast_op_t* channel_op, size_t num_channel_ops,
			     uint8_t* channel_map, size_t max_channel_map,
			     int num_src_channels, int* num_dst_channels);

extern int parse_channel_ops(char* map, acast_op_t* channel_op, size_t max_ops);

extern int parse_channel_map(char* map,
			     acast_op_t* channel_op, size_t max_channel_ops,
			     size_t* num_channel_ops,
			     uint8_t* channel_map, size_t max_channel_map,
			     int num_src_channels, int* num_dst_channels);
#endif
