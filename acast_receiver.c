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
#define LATENCY       1000

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
    uint32_t seqno = 0;
    int err;
    char* snd_function_name = "unknown";
    int tmp;
    int s;
    struct sockaddr_in addr;
    int addrlen;
    char acast_buffer[BYTES_PER_PACKET];
    char silence_buffer[BYTES_PER_PACKET];
    acast_t* acast;
    acast_t* silence;
    uint32_t last_seqno = 0;
    acast_params_t iparam;
    acast_params_t bparam;
    snd_pcm_uframes_t frames_per_packet;
    uint32_t drop = 0;
    uint32_t seen_packet = 0;
    unsigned channels_per_frame = 1;
    uint8_t  channel_map[1] = {1};
    
    if (argc > 1)
	name = argv[1];
    if ((err=snd_pcm_open(&handle,name,SND_PCM_STREAM_PLAYBACK,0)) < 0)
	S_ERR(snd_pcm_open);

    acast_clear_param(&iparam);
    // setup wanted paramters
    iparam.format = SND_PCM_FORMAT_S16_LE;
    iparam.sample_rate = 22000;
    acast_setup_param(handle, &iparam, &bparam, &frames_per_packet);
    acast_print_params(stderr, &bparam);

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
			       frames_per_packet*channels_per_frame);
    
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
	    if (r < 0) {
		perror("recvfrom");
		exit(1);
	    }
	    if (r == 0)
		continue;

	    if (memcmp(&acast->param, &bparam, sizeof(acast_params_t)) != 0) {
		fprintf(stderr, "new parameters\n");
		acast_print(stderr, acast);
	    
		bparam = acast->param;
		snd_pcm_set_params(handle,
				   bparam.format,
				   SND_PCM_ACCESS_RW_INTERLEAVED,
				   channels_per_frame,
				   bparam.sample_rate,
				   SOFT_RESAMPLE,
				   LATENCY);
		frames_per_packet = (BYTES_PER_PACKET - sizeof(acast_t)) /
		    (channels_per_frame * bparam.bytes_per_channel);
		silence->num_frames = frames_per_packet;
		snd_pcm_format_set_silence(bparam.format,silence->data,
					   frames_per_packet*
					   channels_per_frame);
	    }
	    // rearrange data by select channels wanted
	    map_channels(bparam.format,
			 acast->param.channels_per_frame,
			 acast->data,
			 channels_per_frame,
			 acast->data,
			 channel_map,
			 acast->num_frames);
	    
	    snd_pcm_writei(handle, acast->data,
			   (snd_pcm_uframes_t)acast->num_frames);
	    if (seen_packet && ((drop=(acast->seqno - last_seqno)) != 1))
		fprintf(stderr, "dropped %u frames\n", drop);
	    seen_packet = 1;
	    last_seqno = acast->seqno;
	}
    }
    exit(0);

snd_error:
    fprintf(stderr, "sound error %s %s\n",
	    snd_function_name, snd_strerror(err));
    exit(1);    
}
