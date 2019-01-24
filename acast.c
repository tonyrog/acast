// utils

#include <stdio.h>

#include <stdio.h>
#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#include "acast.h"


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

#define S_ERR(name) do { snd_function_name = #name; goto snd_error; } while(0)

// setup audio parameters to use interleaved samples
// and parameters from in, return parameters set in out
int acast_setup_param(snd_pcm_t *handle,
		      acast_params_t* in, acast_params_t* out,
		      snd_pcm_uframes_t* fpp)
{
    const char* snd_function_name = "unknown";
    snd_pcm_hw_params_t *params;
    snd_pcm_format_t fmt;
    int err;
    int tmp;
    snd_pcm_uframes_t frames_per_packet;
    
    snd_pcm_hw_params_alloca(&params);
    
    if ((err = snd_pcm_hw_params_any(handle, params)) < 0)
	S_ERR(snd_pcm_hw_params_any);
    if ((err=snd_pcm_hw_params_set_access(
	     handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
	S_ERR(snd_pcm_hw_params_set_access);

    if (in->format != SND_PCM_FORMAT_UNKNOWN) {
	if ((err=snd_pcm_hw_params_set_format(handle, params, in->format)) < 0)
	    S_ERR(snd_pcm_hw_params_set_format);
    }
    if ((err=snd_pcm_hw_params_get_format(params, &fmt)) < 0)
	S_ERR(snd_pcm_hw_params_get_format);
    
    out->format = fmt;
    out->bits_per_channel = snd_pcm_format_width(fmt);
    out->bytes_per_channel = snd_pcm_format_physical_width(fmt) / 8;
    
    if (in->channels_per_frame) {
	if ((err=snd_pcm_hw_params_set_channels(
		 handle, params, in->channels_per_frame) < 0))
	    S_ERR(snd_pcm_hw_params_set_channels);
    }

    if ((err=snd_pcm_hw_params_get_channels(
	     params, &out->channels_per_frame) < 0))
	S_ERR(snd_pcm_hw_params_get_channels);
    
    if (in->sample_rate) {
	unsigned int val = in->sample_rate;
	if ((err=snd_pcm_hw_params_set_rate_near(
		 handle, params, &val, &tmp)) < 0)
	    S_ERR(snd_pcm_hw_params_set_rate_near);
    }
    if ((err=snd_pcm_hw_params_get_rate(params,
					&out->sample_rate, &tmp))<0) {
	S_ERR(snd_pcm_hw_params_get_rate);
    }
    
    // calculate frames per packet
    frames_per_packet = (BYTES_PER_PACKET - sizeof(acast_t)) /
	(out->channels_per_frame * out->bytes_per_channel);

    if ((err = snd_pcm_hw_params_set_period_size_near(
	     handle, params, &frames_per_packet, &tmp)) < 0)
	S_ERR(snd_pcm_hw_params_set_period_size_near);

    if ((err=snd_pcm_hw_params(handle, params)) < 0)
	S_ERR(snd_pcm_hw_params);
    
    if ((err=snd_pcm_hw_params_get_period_size(params, fpp, &tmp)) < 0)
	S_ERR(snd_pcm_hw_params_get_period_size);	

    return 0;
    
snd_error:
    fprintf(stderr, "sound error %s %s\n",
	    snd_function_name, snd_strerror(err));
    return -1;
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


