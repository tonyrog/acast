// utils


#include <stdio.h>
#include <stdio.h>
#include <ctype.h>
#include <sched.h>

#include "acast.h"

int acast_sender_open(char* maddr, char* ifaddr, int mport,
		      int ttl, int loop,
		      struct sockaddr_in* addr, socklen_t* addrlen,
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
			struct sockaddr_in* addr, socklen_t* addrlen,
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
    fprintf(f, "  format: %s\n", snd_pcm_format_name(param->format));
    fprintf(f, "  channels_per_frame: %u\n", param->channels_per_frame);
    fprintf(f, "  bits_per_channel: %u\n", param->bits_per_channel);
    fprintf(f, "  bytes_per_channel: %u\n", param->bytes_per_channel);
    fprintf(f, "  sample_rate: %u\n", param->sample_rate);
}

void acast_print(FILE* f, acast_t* acast)
{
    int nsamples = 4;
    
    fprintf(f, "seqno: %u\n", acast->seqno);
    fprintf(f, "  num_frames: %u\n", acast->num_frames);    
    acast_print_params(f, &acast->param);
    fprintf(f, "  [frame_time = %.2fms]\n",
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
		uint8_t* buf, size_t len)
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
		  uint8_t* buf, size_t len)
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


static void map_8(uint8_t* src,
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

static void map_16(uint16_t* src,
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

static void map_32(uint32_t* src,
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

// rearrange interleaved channels
// select channels by channel_map from src and put into dst
void map_channels(snd_pcm_format_t fmt,
		  void* src, unsigned int src_channels_per_frame,
		  void* dst, unsigned int dst_channels_per_frame,
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

static void inter_8(uint8_t** src,
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

static void inter_16(uint16_t** src,
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

static void inter_32(uint32_t** src,
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
void interleave_channels(snd_pcm_format_t fmt,
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


static void op_u8(uint8_t* src,
		  unsigned int src_channels_per_frame,
		  uint8_t* dst,
		  unsigned int dst_channels_per_frame,
		  acast_op_t* channel_op,
		  size_t num_ops,		 
		  uint32_t frames)
{
    while(frames--) {
	int i;
	for (i = 0;  i < num_ops; i++) {
	    uint8_t src1 = src[channel_op[i].src1];
	    uint8_t src2 = src[channel_op[i].src2];
	    switch(channel_op[i].op) {
	    case ACAST_OP_SRC1: dst[channel_op[i].dst] = src1; break;
	    case ACAST_OP_SRC2: dst[channel_op[i].dst] = src2; break;
	    case ACAST_OP_ADD:  dst[channel_op[i].dst] = src1+src2; break;
	    case ACAST_OP_SUB:  dst[channel_op[i].dst] = src1-src2; break;
	    }
	}
	dst += dst_channels_per_frame;	
	src += src_channels_per_frame;
    }
}

static void op_i16(int16_t* src,
		   unsigned int src_channels_per_frame,
		   int16_t* dst,		     
		   unsigned int dst_channels_per_frame,
		   acast_op_t* channel_op,
		   size_t num_ops,		 
		   uint32_t frames)
{
    while(frames--) {
	int i;
	for (i = 0;  i < num_ops; i++) {
	    int16_t src1 = src[channel_op[i].src1];
	    int16_t src2 = src[channel_op[i].src2];
	    switch(channel_op[i].op) {
	    case ACAST_OP_SRC1: dst[channel_op[i].dst] = src1; break;
	    case ACAST_OP_SRC2: dst[channel_op[i].dst] = src2; break;
	    case ACAST_OP_ADD: {
		int32_t sum = src1+src2;
		if (sum > 32767) sum = 32767;
		else if (sum < -32768) sum = -32768;
		dst[channel_op[i].dst] = sum;
		break;
	    }
	    case ACAST_OP_SUB: {
		int32_t sum = src1-src2;
		if (sum > 32767) sum = 32767;
		else if (sum < -32768) sum = -32768;
		dst[channel_op[i].dst] = sum;
		break;
	    }
	    }
	}
	dst += dst_channels_per_frame;
	src += src_channels_per_frame;
    }
}


static void op_i32(int32_t* src,
		   unsigned int src_channels_per_frame,
		   int32_t* dst,		     
		   unsigned int dst_channels_per_frame,
		   acast_op_t* channel_op,
		   size_t num_ops,		 
		   uint32_t frames)
{
    while(frames--) {
	int i;
	for (i = 0;  i < num_ops; i++) {
	    int32_t src1 = src[channel_op[i].src1];
	    int32_t src2 = src[channel_op[i].src2];
	    switch(channel_op[i].op) {
	    case ACAST_OP_SRC1: dst[channel_op[i].dst] = src1; break;
	    case ACAST_OP_SRC2: dst[channel_op[i].dst] = src2; break;
	    case ACAST_OP_ADD:  dst[channel_op[i].dst] = src1+src2; break;
	    case ACAST_OP_SUB:  dst[channel_op[i].dst] = src1-src2; break;
	    }
	}
	dst += dst_channels_per_frame;	
	src += src_channels_per_frame;
    }
}

// rearrange interleaved channels with operation
// select input channel src1, src2 and put result in output channel dst
//
void op_channels(snd_pcm_format_t fmt,
		 void* src, unsigned int src_channels_per_frame,
		 void* dst, unsigned int dst_channels_per_frame,
		 acast_op_t* channel_op, size_t num_ops,
		 uint32_t frames)
{
    switch(snd_pcm_format_physical_width(fmt)) {
    case 8:
	op_u8(src, src_channels_per_frame,
	      dst, dst_channels_per_frame,
	      channel_op, num_ops,
	      frames);
	break;
    case 16:
	op_i16((int16_t*)src, src_channels_per_frame,
	       (int16_t*)dst, dst_channels_per_frame,
	       channel_op, num_ops,
	       frames);
	break;	
    case 32:
	op_i32((int32_t*)src, src_channels_per_frame,
	       (int32_t*)dst, dst_channels_per_frame,
	       channel_op, num_ops,	      
	       frames);
	break;
    }
}



static void iop_u8(uint8_t** src,
		   unsigned int src_channels_per_frame,
		   uint8_t* dst,
		   unsigned int dst_channels_per_frame,
		   acast_op_t* channel_op,
		   size_t num_ops,		 
		   uint32_t frames)
{
    int j;
    for (j = 0; j < frames; j++) {
	int i;
	for (i = 0;  i < num_ops; i++) {
	    uint8_t src1 = src[channel_op[i].src1][j];
	    uint8_t src2 = src[channel_op[i].src2][j];
	    switch(channel_op[i].op) {
	    case ACAST_OP_SRC1: dst[channel_op[i].dst] = src1; break;
	    case ACAST_OP_SRC2: dst[channel_op[i].dst] = src2; break;
	    case ACAST_OP_ADD:  dst[channel_op[i].dst] = src1+src2; break;
	    case ACAST_OP_SUB:  dst[channel_op[i].dst] = src1-src2; break;
	    }
	}
	dst += dst_channels_per_frame;	
    }
}

static void iop_i16(int16_t** src,
		    unsigned int src_channels_per_frame,
		    int16_t* dst,		     
		    unsigned int dst_channels_per_frame,
		    acast_op_t* channel_op,
		    size_t num_ops,		 
		    uint32_t frames)
{
    int j;
    for (j = 0; j < frames; j++) {    
	int i;
	for (i = 0;  i < num_ops; i++) {
	    int16_t src1 = src[channel_op[i].src1][j];
	    int16_t src2 = src[channel_op[i].src2][j];
	    switch(channel_op[i].op) {
	    case ACAST_OP_SRC1: dst[channel_op[i].dst] = src1; break;
	    case ACAST_OP_SRC2: dst[channel_op[i].dst] = src2; break;
	    case ACAST_OP_ADD: {
		int32_t sum = src1+src2;
		if (sum > 32767) sum = 32767;
		else if (sum < -32768) sum = -32768;
		dst[channel_op[i].dst] = sum;
		break;
	    }
	    case ACAST_OP_SUB: {
		int32_t sum = src1-src2;
		if (sum > 32767) sum = 32767;
		else if (sum < -32768) sum = -32768;
		dst[channel_op[i].dst] = sum;
		break;
	    }
	    }
	}
	dst += dst_channels_per_frame;
    }
}


static void iop_i32(int32_t** src,
		    unsigned int src_channels_per_frame,
		    int32_t* dst,		     
		    unsigned int dst_channels_per_frame,
		    acast_op_t* channel_op,
		    size_t num_ops,		 
		    uint32_t frames)
{
    int j;
    for (j = 0; j < frames; j++) {        
	int i;
	for (i = 0;  i < num_ops; i++) {
	    int32_t src1 = src[channel_op[i].src1][j];
	    int32_t src2 = src[channel_op[i].src2][j];
	    switch(channel_op[i].op) {
	    case ACAST_OP_SRC1: dst[channel_op[i].dst] = src1; break;
	    case ACAST_OP_SRC2: dst[channel_op[i].dst] = src2; break;
	    case ACAST_OP_ADD:  dst[channel_op[i].dst] = src1+src2; break;
	    case ACAST_OP_SUB:  dst[channel_op[i].dst] = src1-src2; break;
	    }
	}
	dst += dst_channels_per_frame;	
    }
}


// rearrange interleaved channels with operation
// select input channel src1, src2 and put result in output channel dst
//
void iop_channels(snd_pcm_format_t fmt,
		  void** src, unsigned int src_channels_per_frame,  
		  void* dst, unsigned int dst_channels_per_frame,
		  acast_op_t* channel_op, size_t num_ops,
		  uint32_t frames)
{
    switch(snd_pcm_format_physical_width(fmt)) {
    case 8:
	iop_u8((uint8_t**)src, src_channels_per_frame,
	       dst, dst_channels_per_frame,
	       channel_op, num_ops,
	       frames);
	break;
    case 16:
	iop_i16((int16_t**)src, src_channels_per_frame,
		(int16_t*)dst, dst_channels_per_frame,
		channel_op, num_ops,
		frames);
	break;	
    case 32:
	iop_i32((int32_t**)src, src_channels_per_frame,
		(int32_t*)dst, dst_channels_per_frame,
		channel_op, num_ops,	      
		frames);
	break;
    }
}


int parse_channel_ops(char* map, acast_op_t* channel_op, size_t max_ops)
{
    int i = 0;
    char* ptr = map;

    while((i < max_ops) && (*ptr != '\0')) {
	switch(ptr[0]) {
	case '+':
	    if (!isdigit(ptr[1]) || !isdigit(ptr[2]))
		return -1;
	    channel_op[i].src1 = ptr[1]-'0';
	    channel_op[i].src2 = ptr[2]-'0';
	    channel_op[i].dst  = i;
	    channel_op[i].op   = ACAST_OP_ADD;
	    ptr += 3;	    
	    break;
	case '-':
	    if (!isdigit(ptr[1]) || !isdigit(ptr[2]))
		return -1;
	    channel_op[i].src1 = ptr[1]-'0';
	    channel_op[i].src2 = ptr[2]-'0';
	    channel_op[i].dst  = i;
	    channel_op[i].op   = ACAST_OP_SUB;
	    ptr += 3;
	    break;
	default:
	    if (!isdigit(ptr[0]))
		return -1;
	    channel_op[i].src1 = ptr[0]-'0';
	    channel_op[i].src2 = ptr[0]-'0';
	    channel_op[i].dst  = i;
	    channel_op[i].op   = ACAST_OP_SRC1;
	    ptr += 1;	    
	    break;
	}
	i++;
    }
    if (*ptr == '\0')
	return i;
    return -1;
}

void print_channel_ops(acast_op_t* channel_op, size_t num_ops)
{
    int i;
    for (i = 0; i < num_ops; i++) {
	switch(channel_op[i].op) {
	case ACAST_OP_SRC1:
	    printf("%d", channel_op[i].src1);
	    break;
	case ACAST_OP_SRC2:
	    printf("%d", channel_op[i].src2);
	    break;
	case ACAST_OP_ADD:
	    printf("+%d%d", channel_op[i].src1, channel_op[i].src2);
	    break;
	case ACAST_OP_SUB:
	    printf("-%d%d", channel_op[i].src1, channel_op[i].src2);
	    break;
	}
    }
    printf("\n");
}

// try build a simple channel map from channel_op if possible
// return 0 if not possible
// return 1 if simple id channel_map channel_map[i] = i
// return 2 if channel_map channel_map[i] = j where i != j for some i,j
//
int build_channel_map(acast_op_t* channel_op, size_t num_channel_ops,
		      uint8_t* channel_map, size_t max_channel_map,
		      int num_src_channels, int* num_dst_channels)
{
    int i;
    int max_dst_channel = -1;
    int use_channel_map = 1;
    int id_channel_map = 1;
	
    for (i = 0; (i < num_channel_ops) && (i < max_channel_map); i++) {
	if (channel_op[i].dst > max_dst_channel)
	    max_dst_channel = channel_op[i].dst;
	if ((channel_op[i].dst != i) ||
	    (channel_op[i].op != ACAST_OP_SRC1)) {
	    use_channel_map = 0;
	    id_channel_map = 0;
	}
	else if (use_channel_map) {
	    channel_map[i] = channel_op[i].src1;
	    if (channel_map[i] != i)
		id_channel_map = 0;
	}
    }
    if (i >= max_channel_map)
	use_channel_map = 0;
	
    if (*num_dst_channels == 0)
	*num_dst_channels = max_dst_channel+1;
    
    if (*num_dst_channels != num_src_channels)
	id_channel_map = 0;

    if (use_channel_map && id_channel_map)
	return 1;
    else if (use_channel_map)
	return 2;
    return 0;
}


int parse_channel_map(char* map,
		      acast_op_t* channel_op, size_t max_channel_ops,
		      size_t* num_channel_ops,
		      uint8_t* channel_map, size_t max_channel_map,
		      int num_src_channels, int* num_dst_channels)
{
    size_t nc;
    
    if (strcmp(map, "auto") == 0) {
	int i;
	nc = (*num_dst_channels == 0) ? num_src_channels : *num_dst_channels;
	if (nc > max_channel_ops) nc = max_channel_ops;
	for (i = 0; i < nc; i++) {
	    channel_op[i].op = ACAST_OP_SRC1;
	    channel_op[i].src1 = i % num_src_channels;
	    channel_op[i].src2 = 0;
	    channel_op[i].dst = i;
	}
    }
    else {
	if ((nc = parse_channel_ops(map, channel_op, max_channel_ops)) < 0)
	    return -1;
    }
    *num_channel_ops = nc;
    return build_channel_map(channel_op, nc,
			     channel_map, max_channel_map,
			     num_src_channels, num_dst_channels);
}
