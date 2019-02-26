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
#include "tick.h"

// default values
#define MULTICAST_ADDR  "224.0.0.223"
#define MULTICAST_PORT  22402          // audio data
#define INTERFACE_ADDR  "0.0.0.0"
#define ACAST_MAGIC     0x41434147     // "ACAF"
#define CONTROL_PORT    22403          // control data
#define CONTROL_MAGIC   0x41434143     // "ACAC"

#define BYTES_PER_PACKET 1472     // try avoid ip fragmentation

#define MAX_CHANNELS 16

#define MAX_U_32_NUM    0xFFFFFFFF

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
    uint32_t magic;          // identifier magic     
    uint32_t seqno;          // sequence number
    uint32_t num_frames;     // number of frames in packet
    acast_params_t param;    // format params
    uint32_t crc;            // crc32 (seqno,num_frames,param)    
    uint8_t  data[0];        // audio data
} acast_t;

typedef struct
{
    size_t size;                 // number of segments/channels
    size_t stride[MAX_CHANNELS]; // data stride for each pointer
    void*  data[MAX_CHANNELS];   // pointer to each data channel
} acast_buffer_t;

typedef struct
{
    uint32_t magic;          // identifier magic
    uint32_t id;             // use id if id != 0
    uint32_t mask;           // channels requested
    uint32_t crc;            // crc32 (magic, mask)
} actl_t;

#define CLIENT_TIMEOUT 5000000  // 5s

#define CLIENT_MODE_UNICAST   1
#define CLIENT_MODE_MULTICAST 2
#define CLIENT_MODE_MIXED     3

#define PCM_BUFFER_SIZE 1152
#define BYTES_PER_BUFFER ((PCM_BUFFER_SIZE*2*2)+sizeof(acast_t))

typedef struct
{
    uint32_t            id;                   // used if != 0
    struct sockaddr_in  addr;
    socklen_t           addrlen;
    tick_t              tmo;                  // next timeout
    uint32_t            mask;                 // channel mask
    int                 num_output_channels;
    acast_channel_ctx_t chan_ctx;
    uint8_t*            ptr;                  // where to fill
    uint8_t             buffer[2*BYTES_PER_BUFFER];
} client_t;

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
		       void** src, size_t* src_stride, size_t nsrc,
		       void* dst, size_t ndst,
		       uint8_t* channel_map,
		       size_t frames);

extern void scatter_gather_nn(snd_pcm_format_t fmt,
			      void** src, size_t* src_stride, size_t nsrc,
			      void** dst, size_t* dst_stride, size_t ndst,
			      acast_op_t* map, size_t nmao,
			      size_t frames);

extern void scatter_gather_ni(snd_pcm_format_t fmt,
			      void** src, size_t* src_stride, size_t nsrc,
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
