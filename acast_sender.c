//
//  acast_sender
//
//     open a sound device and read data samples
//     and multicast over ip (ttl = 1)
//

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#include "acast.h"

int setup_multicast(char* maddr, char* ifaddr, int mport,
			int ttl, int loop,
			struct sockaddr_in* addr, int* addrlen)
{
    int s;
    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
	uint32_t laddr = inet_addr(ifaddr);
	
	bzero((char *)addr, sizeof(*addr));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = inet_addr(maddr);
	addr->sin_port = htons(mport);
	
	setsockopt(s,IPPROTO_IP,IP_MULTICAST_IF,(void*)&laddr,sizeof(laddr));
	setsockopt(s,IPPROTO_IP,IP_MULTICAST_TTL,(void*)&ttl, sizeof(ttl));
	setsockopt(s,IPPROTO_IP,IP_MULTICAST_LOOP,(void*)&loop, sizeof(loop));	

	*addrlen = sizeof(*addr);
    }
    return s;
}

#define S_ERR(name) do { snd_function_name = #name; goto snd_error; } while(0)

int main(int argc, char** argv)
{
    char* name = "default";    
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    snd_pcm_format_t fmt = SND_PCM_FORMAT_S16_LE;
    unsigned channels_per_frame = 6;
    uint32_t bits_per_channel;
    uint32_t bytes_per_channel;
    uint32_t sample_rate = 22000;
    uint32_t seqno = 0;
    snd_pcm_uframes_t frames_per_packet = 0;
    char* snd_function_name = "unknown";
    int s_error;
    int tmp;
    int s;
    struct sockaddr_in addr;
    int addrlen;
    char acast_buffer[BYTES_PER_PACKET];
    acast_t* acast;
    int multicast_loop = 1;   // loopback multicast packets
    int multicast_ttl  = 0;   // ttl=0 local host, ttl=1 local network

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
    
    bits_per_channel = snd_pcm_format_width(fmt);
    bytes_per_channel = snd_pcm_format_physical_width(fmt) / 8;
    
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

    snd_pcm_hw_params_get_rate(params, &sample_rate, &tmp);
    // calculate frames per packet
    frames_per_packet = (BYTES_PER_PACKET - sizeof(acast_t)) /
	(channels_per_frame * bytes_per_channel);

    if ((s_error = snd_pcm_hw_params_set_period_size_near(
	     handle, params, &frames_per_packet, &tmp)) < 0)
	S_ERR(snd_pcm_hw_params_set_period_size_near);

    if ((s_error=snd_pcm_hw_params(handle, params)) < 0)
	S_ERR(snd_pcm_hw_params_set_period_size_near);

    snd_pcm_hw_params_get_period_size(params, &frames_per_packet, &tmp);

    // fill in "constant" values in the acast header
    acast = (acast_t*) acast_buffer;
    acast->seqno              = seqno;
    acast->num_frames         = 0;
    acast->param.format             = fmt;
    acast->param.channels_per_frame = channels_per_frame; 
    acast->param.bits_per_channel   = bits_per_channel;
    acast->param.bytes_per_channel  = bytes_per_channel;
    acast->param.sample_rate        = sample_rate;

    print_acast(stderr, acast);

    if ((s=setup_multicast(MULTICAST_ADDR, MULTICAST_IF, MULTICAST_PORT,
			   multicast_ttl, multicast_loop,
			   &addr, &addrlen)) < 0) {
	fprintf(stderr, "unable to open multicast socket %s\n",
		strerror(errno));
	exit(1);
    }
    
    while(1) {
	int r;
	size_t nbytes;
	
	r = snd_pcm_readi(handle, acast->data, frames_per_packet);
	nbytes = r * bytes_per_channel * channels_per_frame;
	
	acast->seqno = seqno++;
	acast->num_frames = r;
	if (acast->seqno % 100 == 0)
	    print_acast(stderr, acast);
	sendto(s, acast_buffer, sizeof(acast_t)+nbytes, 0,
	       (struct sockaddr *) &addr, addrlen);
    }
    exit(0);

snd_error:
    fprintf(stderr, "sound error %s %s\n",
	    snd_function_name, snd_strerror(s_error));
    exit(1);
}
