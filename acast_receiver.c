//
//  acast_receiver
//
//     open a sound device for playback and play
//     multicast recieved from mulicast port
//

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#include "acast.h"

#define SOFT_RESAMPLE 1
#define LATENCY       0

int setup_multicast(char* maddr, char* ifaddr, int mport,
		    struct sockaddr_in* addr, int* addrlen)
{
    int s;
    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
	struct ip_mreq mreq;
	int on = 1;
	
	bzero((char *)addr, sizeof(*addr));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = inet_addr(ifaddr);
	addr->sin_port = htons(mport);
	*addrlen = sizeof(*addr);

	setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(void*) &on, sizeof(on));
#ifdef SO_REUSEPORT	
	setsockopt(s,SOL_SOCKET,SO_REUSEPORT,(void*) &on, sizeof(on));
#endif
	mreq.imr_multiaddr.s_addr = inet_addr(maddr);
	mreq.imr_interface.s_addr = inet_addr(ifaddr);
	if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		       &mreq, sizeof(mreq)) < 0) {
	    int err = errno;
	    close(s);
	    errno = err;
	    return -1;
	}
	if (bind(s, (struct sockaddr *) addr, sizeof(*addr)) < 0) {
	    int err = errno;
	    close(s);
	    errno = err;
	    return -1;
	}
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
    char silence_buffer[BYTES_PER_PACKET];
    acast_t* acast;
    acast_t* silence;
    uint32_t last_seqno = 0;
    acast_params_t bparam;
    
    if (argc > 1)
	name = argv[1];
    if ((s_error=snd_pcm_open(&handle, name, SND_PCM_STREAM_PLAYBACK, 0)) < 0)
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

    // this is the startup format
    bparam.format             = fmt;
    bparam.channels_per_frame = channels_per_frame; 
    bparam.bits_per_channel   = bits_per_channel;
    bparam.bytes_per_channel  = bytes_per_channel;
    bparam.sample_rate        = sample_rate;

    if ((s=setup_multicast(MULTICAST_ADDR, MULTICAST_IF, MULTICAST_PORT,
			   &addr, &addrlen)) < 0) {
	fprintf(stderr, "unable to open multicast socket %s\n",
		strerror(errno));
	exit(1);
    }

    acast = (acast_t*) acast_buffer;
    silence =  (acast_t*) silence_buffer;
    silence->num_frames = frames_per_packet;
    snd_pcm_format_set_silence(bparam.format, silence->data,
			       silence->num_frames);
    
    while(1) {
	int r;
	struct pollfd fds;

	fds.fd = s;
	fds.events = POLLIN;

	if ((r = poll(&fds, 1, 0)) == 0) {
	    // write silence
	    snd_pcm_writei(handle, silence->data,
			   (snd_pcm_uframes_t)silence->num_frames);
	}
	else {
	    r = recvfrom(s, acast_buffer, sizeof(acast_buffer), 0, 
			 (struct sockaddr *) &addr, &addrlen);
	    printf("got %d bytes\n", r);
	    if (r < 0) {
		perror("recvfrom");
		exit(1);
	    }
	    if (r == 0)
		continue;

	    if (memcmp(&acast->param, &bparam, sizeof(acast_params_t)) != 0) {
		fprintf(stderr, "new parameters\n");
		bparam = acast->param;
		snd_pcm_set_params(handle,
				   bparam.format,
				   SND_PCM_ACCESS_RW_INTERLEAVED,
				   bparam.channels_per_frame,
				   bparam.sample_rate,
				   SOFT_RESAMPLE,
				   LATENCY);
		frames_per_packet = (BYTES_PER_PACKET - sizeof(acast_t)) /
		    (bparam.channels_per_frame * bparam.bytes_per_channel);
		silence->num_frames = frames_per_packet;
		snd_pcm_format_set_silence(bparam.format, silence->data,
					   silence->num_frames);
	    }
	    snd_pcm_writei(handle, acast->data,
			   (snd_pcm_uframes_t)acast->num_frames);
	    if (acast->seqno % 100 == 0) {
		print_acast(stderr, acast);
	    }
	    last_seqno = acast->seqno;
	}
    }
    exit(0);

snd_error:
    fprintf(stderr, "sound error %s %s\n",
	    snd_function_name, snd_strerror(s_error));
    exit(1);    
}
