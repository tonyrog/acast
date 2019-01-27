// utils


#include <stdio.h>
#include <stdio.h>
#include <sched.h>

#include "acast.h"

int acast_sender_open(char* maddr, char* ifaddr, int mport,
		      int ttl, int loop,
		      struct sockaddr_in* addr, int* addrlen,
		      size_t bufsize)
{
    int sock;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
	uint32_t laddr = inet_addr(ifaddr);
	int val;
	bzero((char *)addr, sizeof(*addr));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = inet_addr(maddr);
	addr->sin_port = htons(mport);
	
	setsockopt(sock,IPPROTO_IP,IP_MULTICAST_IF,(void*)&laddr,sizeof(laddr));
	setsockopt(sock,IPPROTO_IP,IP_MULTICAST_TTL,(void*)&ttl,sizeof(ttl));
	setsockopt(sock,IPPROTO_IP,IP_MULTICAST_LOOP,(void*)&loop,sizeof(loop));
	val = bufsize;
	setsockopt(sock,SOL_SOCKET,SO_SNDBUF, &val,sizeof(val));

	*addrlen = sizeof(*addr);
    }
    return sock;
}

int acast_receiver_open(char* maddr, char* ifaddr, int mport,
			struct sockaddr_in* addr, int* addrlen,
			size_t bufsize)
{
    int sock;

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
	struct ip_mreq mreq;
	int on = 1;
	int val;
	
	bzero((char *)addr, sizeof(*addr));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = inet_addr(ifaddr);
	addr->sin_port = htons(mport);
	*addrlen = sizeof(*addr);

	setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(void*) &on, sizeof(on));
#ifdef SO_REUSEPORT	
	setsockopt(sock,SOL_SOCKET,SO_REUSEPORT,(void*) &on, sizeof(on));
#endif
	val = (int) bufsize;
	setsockopt(sock,SOL_SOCKET,SO_RCVBUF, &val, sizeof(val));
	
	mreq.imr_multiaddr.s_addr = inet_addr(maddr);
	mreq.imr_interface.s_addr = inet_addr(ifaddr);
	if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		       &mreq, sizeof(mreq)) < 0) {
	    int err = errno;
	    close(sock);
	    errno = err;
	    return -1;
	}
	if (bind(sock, (struct sockaddr *) addr, sizeof(*addr)) < 0) {
	    int err = errno;
	    close(sock);
	    errno = err;
	    return -1;
	}
    }
    return sock;
}


void acast_clear_param(acast_params_t* in)
{
    in->format = -1;
    in->channels_per_frame = 0;
    in->bits_per_channel = 0;
    in->bytes_per_channel = 0;
    in->sample_rate = 0;
}

void acast_print_params(FILE* f, acast_params_t* param)
{
    fprintf(f, "format: %s\n", snd_pcm_format_name(param->format));
    fprintf(f, "channels_per_frame: %u\n", param->channels_per_frame);
    fprintf(f, "bits_per_channel: %u\n", param->bits_per_channel);
    fprintf(f, "bytes_per_channel: %u\n", param->bytes_per_channel);
    fprintf(f, "sample_rate: %u\n", param->sample_rate);
}

void acast_print(FILE* f, acast_t* acast)
{
    int nsamples = 4;
    
    fprintf(f, "seqno: %u\n", acast->seqno);
    fprintf(f, "num_frames: %u\n", acast->num_frames);    
    acast_print_params(f, &acast->param);
    fprintf(f, "[frame_time = %.2fms]\n",
	    1000*(1.0/acast->param.sample_rate)*acast->num_frames);
    // print first frame only (debugging)

    switch(snd_pcm_format_physical_width(acast->param.format)) {
    case 8: {
	uint8_t* fp = (uint8_t*) acast->data;
	int s;
	for (s = 0; s < nsamples; s++) {
	    int i;
	    fprintf(f, "data[%d]: ", s);
	    for (i = 0; i < acast->param.channels_per_frame; i++)
		fprintf(f, "%02x ", fp[i]);
	    fprintf(f, "\n");	    
	    fp += acast->param.channels_per_frame;
	}
	break;
    }
    case 16: {
	uint16_t* fp = (uint16_t*) acast->data;
	int s;	
	for (s = 0; s < nsamples; s++) {
	    int i;	    
	    fprintf(f, "data[%d]: ", s);	    
	    for (i = 0; i < acast->param.channels_per_frame; i++)
		fprintf(f, "%04x ", fp[i]);
	    fprintf(f, "\n");	    
	    fp += acast->param.channels_per_frame;
	}
	break;
    }
    case 32: {
	uint32_t* fp = (uint32_t*) acast->data;
	int s;
	for (s = 0; s < nsamples; s++) {
	    int i;
	    fprintf(f, "data[%d]: ", s);	    	    
	    for (i = 0; i < acast->param.channels_per_frame; i++)
		fprintf(f, "%08x ", fp[i]);
	    fprintf(f, "\n");
	    fp += acast->param.channels_per_frame;
	}
	break;
    }
    default:
	break;
    }
}

// calculate frames per packet
snd_pcm_uframes_t acast_get_frames_per_packet(acast_params_t* pp)
{
    return (BYTES_PER_PACKET - sizeof(acast_t)) /
	(pp->channels_per_frame * pp->bytes_per_channel);
}


#define SNDCALL(name, args...)						\
    do {								\
        int err;							\
	if ((err = name(args)) < 0) {					\
	acast_emit_error(stderr, __FILE__, __LINE__, #name, err);	\
	return -1;							\
	}								\
    } while(0)

void acast_emit_error(FILE* f, char* file, int line, char* function, int err)
{
    fprintf(f, "%s:%d: snd error %s %s\n",
	    file, line, function, snd_strerror(err));
}

// setup audio parameters to use interleaved samples
// and parameters from in, return parameters set in out
int acast_setup_param(snd_pcm_t *handle,
		      acast_params_t* in, acast_params_t* out,
		      snd_pcm_uframes_t* fpp)
{
    const char* snd_function_name = "unknown";
    snd_pcm_hw_params_t *params;
    snd_pcm_sw_params_t *sparams;
    snd_pcm_format_t fmt;
    snd_pcm_uframes_t frames_per_packet;
    snd_pcm_uframes_t buffersize;
    snd_pcm_uframes_t periodsize;
    snd_pcm_uframes_t val;

    snd_pcm_hw_params_alloca(&params);
    snd_pcm_sw_params_alloca(&sparams);

    SNDCALL(snd_pcm_hw_params_any, handle, params);
    
    SNDCALL(snd_pcm_hw_params_set_access,
	    handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

    if (in->format != SND_PCM_FORMAT_UNKNOWN)
	SNDCALL(snd_pcm_hw_params_set_format,handle,params,in->format);
    
    SNDCALL(snd_pcm_hw_params_get_format,params,&fmt);
    
    out->format = fmt;
    out->bits_per_channel = snd_pcm_format_width(fmt);
    out->bytes_per_channel = snd_pcm_format_physical_width(fmt) / 8;
    
    if (in->channels_per_frame)
	SNDCALL(snd_pcm_hw_params_set_channels,
		handle, params, in->channels_per_frame);

    SNDCALL(snd_pcm_hw_params_get_channels,params,&out->channels_per_frame);
    
    if (in->sample_rate) {
	unsigned int val = in->sample_rate;
	SNDCALL(snd_pcm_hw_params_set_rate_near, handle, params, &val, 0);
    }

    SNDCALL(snd_pcm_hw_params_get_rate, params, &out->sample_rate, 0);
    
    frames_per_packet = acast_get_frames_per_packet(out);

    buffersize = frames_per_packet*4;  // or more
    SNDCALL(snd_pcm_hw_params_set_buffer_size_near,handle,params,&buffersize);

    periodsize = buffersize / 2;

    SNDCALL(snd_pcm_hw_params_set_period_size_near,handle,params,&periodsize,0);

    SNDCALL(snd_pcm_hw_params, handle, params);

    // set soft params

    SNDCALL(snd_pcm_sw_params_current,handle,sparams);

    SNDCALL(snd_pcm_sw_params_set_start_threshold,handle,sparams,0x7fffffff);

    SNDCALL(snd_pcm_hw_params_get_period_size, params, &val, 0);
    
    SNDCALL(snd_pcm_sw_params_set_avail_min, handle, sparams, val);
    SNDCALL(snd_pcm_sw_params, handle, sparams);

    SNDCALL(snd_pcm_prepare, handle);
    
    *fpp = frames_per_packet;
    return 0;
}

long acast_play(snd_pcm_t* handle, size_t bytes_per_frame,
		 char* buf, size_t len)
{
    long r;
    long len0 = len;
    
    while(len > 0) {
	do {
	    r = snd_pcm_writei(handle, buf, len);
	} while(r == -EAGAIN);
	if (r < 0)
	    return r;
	if (bytes_per_frame == 0)
	    return r;
	buf += r*bytes_per_frame;
	len -= (size_t) r;
    }
    return len0;
}

long acast_record(snd_pcm_t* handle, size_t bytes_per_frame,
		  char* buf, size_t len)
{
    long r;
    long len0 = len;
    
    while(len > 0) {
	do {
	    r = snd_pcm_readi(handle, buf, len);
	} while (r == -EAGAIN);
	if (r < 0)
	    return r;
	if (bytes_per_frame == 0)
	    return r;
	buf += r * bytes_per_frame;
	len -= (size_t) r;
    }
    return len0;
}


void acast_setscheduler(void)
{
    struct sched_param sched_param;
    
    if (sched_getparam(0, &sched_param) < 0) {
	fprintf(stderr, "sched_getparam failed: %s\n",
		strerror(errno));
	return;
    }
    
    sched_param.sched_priority = sched_get_priority_max(SCHED_RR);
    
    if (sched_setscheduler(0, SCHED_RR, &sched_param) < 0) {
	fprintf(stderr, "sched_setscheduler failed: %s\n",
		strerror(errno));	
	return;	
    }
    printf("scheduler set to Round Robin with priority %d\n",
	   sched_param.sched_priority);
}


static int map_8(uint8_t* src,
		  unsigned int src_channels_per_frame,
		  uint8_t* dst,		     
		  unsigned int dst_channels_per_frame,
		  uint8_t* channel_map,
		  uint32_t frames)
{
    while(frames--) {
	int i;
	for (i = 0;  i < dst_channels_per_frame; i++)
	    *dst++ = src[channel_map[i]];
	src += src_channels_per_frame;
    }
}

static int map_16(uint16_t* src,
		   unsigned int src_channels_per_frame,
		   uint16_t* dst,
		   unsigned int dst_channels_per_frame,
		   uint8_t* channel_map,
		   uint32_t frames)
{
    while(frames--) {
	int i;
	for (i = 0;  i < dst_channels_per_frame; i++)
	    *dst++ = src[channel_map[i]];
	src += src_channels_per_frame;
    }
}

static int map_32(uint32_t* src,
		  unsigned int src_channels_per_frame,
		  uint32_t* dst,		     
		  unsigned int dst_channels_per_frame,
		  uint8_t* channel_map,
		  uint32_t frames)
{
    while(frames--) {
	int i;
	for (i = 0;  i < dst_channels_per_frame; i++)
	    *dst++ = src[channel_map[i]];
	src += src_channels_per_frame;
    }
}

// rearange interleaved channels
// select channels by channel_map from src and put into dst
int map_channels(snd_pcm_format_t fmt,
		 unsigned int src_channels_per_frame,
		 void* src,
		 unsigned int dst_channels_per_frame,		  
		 void* dst,
		 uint8_t* channel_map,
		 uint32_t frames)
{
    switch(snd_pcm_format_physical_width(fmt)) {
    case 8:
	map_8(src, src_channels_per_frame,
	      dst, dst_channels_per_frame,
	      channel_map, frames);
	break;
    case 16:
	map_16((uint16_t*)src, src_channels_per_frame,
	       (uint16_t*)dst, dst_channels_per_frame,
	       channel_map, frames);
	break;	
    case 32:
	map_32((uint32_t*)src, src_channels_per_frame,
	       (uint32_t*)dst, dst_channels_per_frame,
	       channel_map, frames);
	break;
    }
}

static int inter_8(uint8_t** src,
		   unsigned int dst_channels_per_frame,
		   uint8_t* dst,
		   uint8_t* channel_map,
		   uint32_t frames)
{
    int s, i;

    for (s = 0; s < frames; s++)
	for (i = 0; i < dst_channels_per_frame; i++)
	    *dst++ = src[channel_map[i]][s];
}

static int inter_16(uint16_t** src,
		    unsigned int dst_channels_per_frame,
		    uint16_t* dst,
		    uint8_t* channel_map,
		    uint32_t frames)
{
    int s, i;

    for (s = 0; s < frames; s++)
	for (i = 0; i < dst_channels_per_frame; i++)
	    *dst++ = src[channel_map[i]][s];
}

static int inter_32(uint32_t** src,
		    unsigned int dst_channels_per_frame,
		    uint32_t* dst,
		    uint8_t* channel_map,
		    uint32_t frames)
{
    int s, i;

    for (s = 0; s < frames; s++)
	for (i = 0; i < dst_channels_per_frame; i++)
	    *dst++ = src[channel_map[i]][s];
}

// interleave channels using channel map
int interleave_channels(snd_pcm_format_t fmt,
			void** src,
			unsigned int dst_channels_per_frame,
			void* dst,
			uint8_t* channel_map,
			uint32_t frames)
{
    switch(snd_pcm_format_physical_width(fmt)) {
    case 8:
	inter_8((uint8_t**) src,
		dst_channels_per_frame,
		dst, channel_map, frames);
	break;
    case 16:
	inter_16((uint16_t**) src,
		 dst_channels_per_frame,
		 (uint16_t*)dst, channel_map, frames);
	break;
    case 32:
	inter_32((uint32_t**) src,
		 dst_channels_per_frame,
		 (uint32_t*)dst, channel_map, frames);
	break;
    }
}


