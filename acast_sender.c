//
//  acast_sender
//
//     open a sound device and read data samples
//     and multicast over ip (ttl = 1)
//
//   BYTES_PER_PACKET (1500) is limited so each packet fit in
//    a wifi packet with no IP fragmentation
//   FRAMES_PER_PACKET N
//   CHANNELS_PER_FRAME
//   BITS_PER_CHANNEL
//  
#include <stdio.h>
#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#define BYTES_PER_PACKET 1472

int channel_bits(snd_pcm_format_t fmt)
{
    switch(fmt) {
	// signed - float
    case SND_PCM_FORMAT_FLOAT_LE:   return 32;
    case SND_PCM_FORMAT_FLOAT_BE:   return 32;
    case SND_PCM_FORMAT_FLOAT64_LE: return 64;
    case SND_PCM_FORMAT_FLOAT64_BE: return 64;
	// signed - integer	
    case SND_PCM_FORMAT_S8:     return 8;	
    case SND_PCM_FORMAT_S16_LE: return 16;
    case SND_PCM_FORMAT_S24_LE: return 24;
    case SND_PCM_FORMAT_S32_LE: return 32;
    case SND_PCM_FORMAT_S16_BE: return 16;
    case SND_PCM_FORMAT_S24_BE: return 24;
    case SND_PCM_FORMAT_S32_BE: return 32;
	// unsigned integer
    case SND_PCM_FORMAT_U8:     return 8;	
    case SND_PCM_FORMAT_U16_LE: return 16;
    case SND_PCM_FORMAT_U24_LE: return 24;
    case SND_PCM_FORMAT_U32_LE: return 32;
    case SND_PCM_FORMAT_U16_BE: return 16;
    case SND_PCM_FORMAT_U24_BE: return 24;
    case SND_PCM_FORMAT_U32_BE: return 32;
    default: return 0;
    }
}

int channel_bytes(snd_pcm_format_t fmt)
{
    return channel_bits(fmt) >> 3;
}

#define S_ERR(name) do { snd_function_name = #name; goto snd_error; } while(0)


int main(int argc, char** argv)
{
    char* name = "default";    
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    snd_pcm_format_t fmt = SND_PCM_FORMAT_S16_LE;
    unsigned channels_per_frame = 6;
    float sample_rate = 22000;
    snd_pcm_uframes_t frames = 0;
    char* snd_function_name = "unknown";
    int s_error;
    int tmp;
    char buffer[BYTES_PER_PACKET];
    
    if (argc > 1)
	name = argv[1];
    if ((s_error=snd_pcm_open(&handle, name, SND_PCM_STREAM_CAPTURE, 0)) < 0)
	S_ERR(snd_pcm_open);
    snd_pcm_hw_params_alloca(&params);
    if ((s_error = snd_pcm_hw_params_any(handle, params)) < 0)
	S_ERR(snd_pcm_hw_params_any);	
    if ((s_error=snd_pcm_hw_params_set_access(
	     handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
	S_ERR(snd_pcm_hw_params_set_access);		
    if ((s_error=snd_pcm_hw_params_set_format(handle, params, fmt)) < 0)
	S_ERR(snd_pcm_hw_params_set_format);
    if (channels_per_frame) {
	if ((s_error=snd_pcm_hw_params_set_channels(
		 handle, params, channels_per_frame) < 0))
	    S_ERR(snd_pcm_hw_params_set_channels);
    }
    if (sample_rate) {
	unsigned int val = sample_rate;
	if ((s_error=snd_pcm_hw_params_set_rate_near(
		 handle, params, &val, &tmp)) < 0)
	    S_ERR(snd_pcm_hw_params_set_rate_near);
    }
    // calculate frames per packet
    frames = BYTES_PER_PACKET / (channels_per_frame * channel_bytes(fmt));

    if ((s_error = snd_pcm_hw_params_set_period_size_near(
	     handle, params, &frames, &tmp)) < 0)
	S_ERR(snd_pcm_hw_params_set_period_size_near);

    if ((s_error=snd_pcm_hw_params(handle, params)) < 0)
	S_ERR(snd_pcm_hw_params_set_period_size_near);

    snd_pcm_hw_params_get_period_size(params, &frames, &tmp);

    printf("frames = %lu, rate=%.2f\n", frames, sample_rate);
    printf("frame delay = %.2fms\n", 1000*(1/sample_rate)*frames);
    printf("packet bytes = %ld\n",frames*channels_per_frame*channel_bytes(fmt));
    
    while(1) {
	int r;
	uint16_t* fp;
	r = snd_pcm_readi(handle, buffer, frames);
	fp = (int16_t*) buffer;
	printf("[%04x][%04x][%04x][%04x][%04x][%04x]\n",
	       fp[0], fp[1], fp[2], fp[3], fp[4], fp[5]);
    }
    exit(0);

snd_error:
    fprintf(stderr, "sound error %s %s\n",
	    snd_function_name, snd_strerror(s_error));
    exit(1);
}
