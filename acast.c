// utils


#include <stdio.h>
#include <stdio.h>
#include <ctype.h>
#include <sched.h>

#include "acast.h"
#include "acast_channel.h"
#include "g711.h"

#define DEBUG

int acast_sender_open(char* maddr, char* ifaddr, int mport,
		      int ttl, int loop,
		      struct sockaddr_in* addr, socklen_t* addrlen,
		      size_t bufsize)
{
    int sock;
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
	struct in_addr laddr;
	int val;
	socklen_t len;
	
	if (!inet_aton(ifaddr, &laddr)) {
	    fprintf(stderr, "ifaddr syntax error [%s]\n", ifaddr);
	    return -1;
	}

	if (setsockopt(sock,IPPROTO_IP,IP_MULTICAST_IF,
		       (void*)&laddr,sizeof(laddr)) < 0) {
	    perror("setsockopt: IP_MULTICAST_IF");
	}
	    
	if (setsockopt(sock,IPPROTO_IP,IP_MULTICAST_TTL,
		       (void*)&ttl,sizeof(ttl)) < 0) {
	    perror("setsockopt: IP_MULTICAST_TTL");
	}
	    
	if (setsockopt(sock,IPPROTO_IP,IP_MULTICAST_LOOP,
		       (void*)&loop,sizeof(loop)) < 0) {
	    perror("setsockopt: IP_MULTICAST_LOOP");
	}

	memset((char *)addr, 0, sizeof(*addr));	
	addr->sin_family = AF_INET;
	if (!inet_aton(maddr, &addr->sin_addr)) {
	    fprintf(stderr, "maddr syntax error [%s]\n", maddr);
	    return -1;
	}
	addr->sin_port = htons(mport);

	val = bufsize;
	if (setsockopt(sock,SOL_SOCKET,SO_SNDBUF,
		       (void*)&val,sizeof(val)) < 0) {
	    perror("setsockopt: SO_SNDBUF");
	}
#ifdef DEBUG
	len = sizeof(val);
	if (getsockopt(sock,SOL_SOCKET,SO_SNDBUF,
		       (void*)&val,&len) < 0) {
	    perror("getsockopt: SO_SNDBUF");
	}
	fprintf(stderr, "SNDBUF = %d\n", val);
#endif
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
	socklen_t len;
	
	if (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(void*)&on,sizeof(on))<0) {
	    perror("setsockopt: REUSEADDR");
	}
#ifdef SO_REUSEPORT
	if (setsockopt(sock,SOL_SOCKET,SO_REUSEPORT,(void*)&on,sizeof(on))<0){
	    perror("setsockopt: REUSEPORT");
	}
#endif
	val = (int) bufsize;
	if (setsockopt(sock,SOL_SOCKET,SO_RCVBUF, &val, sizeof(val))<0) {
	    perror("setsockopt: RCVBUF");
	}
#ifdef DEBUG
	len = sizeof(val);	
	if (getsockopt(sock,SOL_SOCKET,SO_RCVBUF,(void*)&val,&len) < 0) {
	perror("getsockopt: SO_RCVBUF");
	}
	fprintf(stderr, "RCVBUF = %d\n", val);
#endif	

	memset((char *)addr, 0, sizeof(*addr));
	addr->sin_family = AF_INET;

	if (!inet_aton("0.0.0.0", &addr->sin_addr)) {
	    fprintf(stderr, "ifaddr syntax error [%s]\n", ifaddr);
	    return -1;
	}
	addr->sin_port = htons(mport);
	*addrlen = sizeof(*addr);

	if (bind(sock, (struct sockaddr *) addr, sizeof(*addr)) < 0) {
	    int err = errno;
	    perror("bind");
	    close(sock);
	    errno = err;
	    return -1;
	}

	// FIXME add SO_BINDTODEVICE for multicast?

	if (!inet_aton(maddr, &mreq.imr_multiaddr)) {
	    fprintf(stderr, "maddr syntax error [%s]\n", maddr);
	    return -1;
	}
	if (!inet_aton(ifaddr, &mreq.imr_interface)) {
	    fprintf(stderr, "ifaddr syntax error [%s]\n", ifaddr);
	    return -1;
	}
	if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		       &mreq, sizeof(mreq)) < 0) {
	    int err = errno;
	    perror("setsockopt: IP_ADD_MEMBERSHIP");
	    close(sock);
	    errno = err;
	    return -1;
	}
    }
    return sock;
}

int acast_usender_open(char* uaddr, char* ifaddr, int port,
		       struct sockaddr_in* addr, socklen_t* addrlen,
		       size_t bufsize)
{
    int sock;

    memset((char *)addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    if (!inet_aton(uaddr, &addr->sin_addr)) {
	fprintf(stderr, "address syntax error [%s]\n", uaddr);
	return -1;
    }
    addr->sin_port = htons(port);
    *addrlen = sizeof(*addr);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
	int val;
	socklen_t len;
	struct sockaddr_in baddr;
	
	memset(&baddr, 0, sizeof(baddr));
	baddr.sin_family = AF_INET;
	baddr.sin_port   = htons(0);
	if (!inet_aton(ifaddr, &baddr.sin_addr)) {
	    fprintf(stderr, "ifaddr syntax error [%s]\n", ifaddr);
	    return -1;
	}
	if (bind(sock, (struct sockaddr *)&baddr, sizeof(baddr)) < 0) {
	    int err = errno;
	    perror("bind");
	    close(sock);
	    errno = err;
	    return -1;
	}
	val = bufsize;
	if (setsockopt(sock,SOL_SOCKET,SO_SNDBUF,
		       (void*)&val,sizeof(val)) < 0) {
	    perror("setsockopt: SO_SNDBUF");
	}
#ifdef DEBUG
	len = sizeof(val);
	if (getsockopt(sock,SOL_SOCKET,SO_SNDBUF,
		       (void*)&val,&len) < 0) {
	    perror("getsockopt: SO_SNDBUF");
	}
	fprintf(stderr, "SNDBUF = %d\n", val);
#endif
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
    unsigned int uval;
    snd_pcm_uframes_t ufval;

    snd_pcm_drop(handle);
    
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

    SNDCALL(snd_pcm_hw_params_get_channels,params,&uval);
    out->channels_per_frame = uval;
    
    if (in->sample_rate) {
	uval = in->sample_rate;
	SNDCALL(snd_pcm_hw_params_set_rate_near, handle, params, &uval, 0);
    }

    SNDCALL(snd_pcm_hw_params_get_rate, params, &uval, 0);
    out->sample_rate = uval;
    
    frames_per_packet = acast_get_frames_per_packet(out);

    buffersize = frames_per_packet*4;  // or more
    SNDCALL(snd_pcm_hw_params_set_buffer_size_near,handle,params,&buffersize);

    periodsize = buffersize / 2;

    SNDCALL(snd_pcm_hw_params_set_period_size_near,handle,params,&periodsize,0);

    SNDCALL(snd_pcm_hw_params, handle, params);

    // set soft params

    SNDCALL(snd_pcm_sw_params_current,handle,sparams);

    SNDCALL(snd_pcm_sw_params_set_start_threshold,handle,sparams,0x7fffffff);

    SNDCALL(snd_pcm_hw_params_get_period_size, params, &ufval, 0);
    SNDCALL(snd_pcm_sw_params_set_avail_min, handle, sparams, ufval);

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

//
// import scatter_gather_uint8, scatter_gather_int16, scatter_gather_uint32
//
#define TYPE uint8_t
#define TYPE2 uint16_t
#include "map.i"

#define TYPE int16_t
#define TYPE2 int32_t
#include "map.i"

#define TYPE int32_t
#define TYPE2 int64_t
#include "map.i"

// rearrange interleaved channels
// select channels by channel_map from src and put into dst
void permute_ii(snd_pcm_format_t fmt,
		void* src, size_t nsrc,
		void* dst, size_t ndst,
		uint8_t* channel_map,
		size_t frames)
{
    switch(snd_pcm_format_physical_width(fmt)) {
    case 8:
	permute_ii_uint8_t(
	    (uint8_t*)src, nsrc,
	    (uint8_t*)dst, ndst,
	    channel_map, frames);
	break;
    case 16:
	permute_ii_int16_t(
	    (int16_t*)src, nsrc,
	    (int16_t*)dst, ndst,
	    channel_map, frames);
	break;	
    case 32:
	permute_ii_int32_t(	
	    (int32_t*)src, nsrc,
	    (int32_t*)dst, ndst,
	    channel_map, frames);
	break;
    }
}

// interleave channels using channel map
void permute_ni(snd_pcm_format_t fmt,
		void** src, size_t* src_stride, size_t nsrc,
		void* dst, size_t ndst,
		uint8_t* channel_map,
		size_t frames)
{
    switch(snd_pcm_format_physical_width(fmt)) {
    case 8:
	permute_ni_uint8_t(
	    (uint8_t**) src, src_stride, nsrc,
	    (uint8_t*) dst, ndst,
	    channel_map, frames);
	break;
    case 16:
	permute_ni_int16_t(
	    (int16_t**) src, src_stride, nsrc,
	    (int16_t*) dst, ndst,
	    channel_map, frames);
	break;
    case 32:
	permute_ni_int32_t(
	    (int32_t**) src, src_stride, nsrc,
	    (int32_t*) dst, ndst,
	    channel_map, frames);
	break;
    }
}

// rearrange interleaved channels with operation
// select input channel src1, src2 and put result in output channel dst
//
void scatter_gather_ii(snd_pcm_format_t fmt,
		       void* src, size_t src_stride,
		       void* dst, size_t dst_stride,
		       acast_op_t* map, size_t nmap,
		       size_t frames)
{
    switch(snd_pcm_format_physical_width(fmt)) {
    case 8:
	scatter_gather_ii_uint8_t(
	    (uint8_t*)src, src_stride,
	    (uint8_t*)dst, dst_stride,
	    map, nmap,
	    frames);
	break;
    case 16:
	scatter_gather_ii_int16_t(
	    (int16_t*)src, src_stride,
	    (int16_t*)dst, dst_stride,
	    map, nmap,
	    frames);
	break;	
    case 32:
	scatter_gather_ii_int32_t(
	    (int32_t*)src, src_stride,
	    (int32_t*)dst, dst_stride,
	    map, nmap,
	    frames);
	break;
    }
}

// operate on separate channels in src and result in interleaved channels
// in dst.
void scatter_gather_ni(snd_pcm_format_t fmt,
		       void** src, size_t* src_stride, size_t nsrc,
		       void* dst, size_t dst_stride,
		       acast_op_t* channel_op, size_t num_ops,
		       size_t frames)
{
    switch(snd_pcm_format_physical_width(fmt)) {
    case 8:
	scatter_gather_ni_uint8_t(
	    (uint8_t**)src, src_stride, nsrc,
	    (uint8_t*)dst, dst_stride,
	    channel_op, num_ops,
	    frames);
	break;
    case 16:
	scatter_gather_ni_int16_t(
	    (int16_t**)src, src_stride, nsrc,
	    (int16_t*)dst, dst_stride,
	    channel_op, num_ops,
	    frames);
	break;	
    case 32:
	scatter_gather_ni_int32_t(
	    (int32_t**)src, src_stride, nsrc,
	    (int32_t*)dst, dst_stride,
	    channel_op, num_ops,	      
	    frames);
	break;
    }
}

void scatter_gather_nn(snd_pcm_format_t fmt,
		       void** src, size_t* src_stride, size_t nsrc,
		       void** dst, size_t* dst_stride, size_t ndst,
		       acast_op_t* channel_op, size_t num_ops,
		       size_t frames)
{
    switch(snd_pcm_format_physical_width(fmt)) {
    case 8:
	scatter_gather_nn_uint8_t(
	    (uint8_t**)src, src_stride, nsrc,
	    (uint8_t**)dst, dst_stride, ndst,
	    channel_op, num_ops,
	    frames);
	break;
    case 16:
	scatter_gather_nn_int16_t(
	    (int16_t**)src, src_stride, nsrc,
	    (int16_t**)dst, dst_stride, ndst,
	    channel_op, num_ops,
	    frames);
	break;	
    case 32:
	scatter_gather_nn_int32_t(
	    (int32_t**)src, src_stride, nsrc,
	    (int32_t**)dst, dst_stride, ndst,
	    channel_op, num_ops,	      
	    frames);
	break;
    }
}
