//
//  acast_receiver
//
//     open a sound device for playback and play
//     multicast recieved from mulicast port
//
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "acast.h"

#define PLAYBACK_DEVICE "default"
#define NUM_CHANNELS  2
#define CHANNEL_MAP   "01"

void help(void)
{
printf("usage: mp3_sender [options] file\n"
"  -h, --help      print help\n"
"  -v, --verbose   increase verbosity\n"
"  -D, --debug     debug verbosity\n"              
"  -a, --addr      multicast address (%s)\n"
"  -i, --iface     multicast interface address (%s)\n"
"  -p, --port      multicast address port (%d)\n"
"  -d, --device    playback device (%s)\n"
"  -c, --channels  number of output channels (%d)\n"
"  -m, --map       channel map (%s)\n",       
       MULTICAST_ADDR,
       MULTICAST_IFADDR,
       MULTICAST_PORT,
       PLAYBACK_DEVICE,
       NUM_CHANNELS,
       CHANNEL_MAP);       
}

int verbose = 0;
int debug = 0;

int main(int argc, char** argv)
{
    char* playback_device_name = PLAYBACK_DEVICE;
    snd_pcm_t *handle;
    int err;
    int sock;
    int len;
    char* multicast_addr = MULTICAST_ADDR;
    char* multicast_ifaddr = MULTICAST_IFADDR;    // interface address
    uint16_t multicast_port = MULTICAST_PORT;
    struct sockaddr_in addr;
    socklen_t addrlen;
    uint8_t acast_buffer[BYTES_PER_PACKET];
    uint8_t silence_buffer[BYTES_PER_PACKET];
    uint8_t snd_buffer[BYTES_PER_PACKET];
    acast_t* acast;
    acast_t* silence;
    uint32_t last_seqno = 0;
    acast_params_t iparam;
    acast_params_t oparam;
    acast_params_t lparam;
    snd_pcm_uframes_t frames_per_packet;
    uint32_t drop = 0;
    uint32_t seen_packet = 0;
    int num_output_channels = NUM_CHANNELS;    
    uint8_t  channel_map[8] = {0,1,0,1,0,0,0,0};
    size_t bytes_per_frame;
    size_t network_bufsize = 4*BYTES_PER_PACKET;
    int mode = SND_PCM_NONBLOCK;

    while(1) {
	int option_index = 0;
	int c;
	static struct option long_options[] = {
	    {"help",   no_argument, 0,       'h'},
	    {"verbose",no_argument, 0,       'v'},
	    {"addr",   required_argument, 0, 'a'},
	    {"iface",  required_argument, 0, 'i'},
	    {"port",   required_argument, 0, 'p'},
	    {"device", required_argument, 0, 'd'},
	    {0,        0,                 0, 0}
	};
	
	c = getopt_long(argc, argv, "lhva:i:p:t:d:",
                        long_options, &option_index);
	if (c == -1)
	    break;
	switch(c) {
	case 'h':
	    help();
	    exit(0);
	    break;
	case 'v':
	    verbose++;
	    break;
	case 'd':
	    playback_device_name = strdup(optarg);
	    break;
	case 'a':
	    multicast_addr = strdup(optarg);
	    break;
	case 'i':
	    multicast_ifaddr = strdup(optarg);
	    break;
	case 'p':
	    multicast_port = atoi(optarg);
	    if ((multicast_port < 1) || (multicast_port > 65535)) {
		fprintf(stderr, "multicast port out of range\n");
		exit(1);
	    }
	    break;
	}
    }

    if ((err=snd_pcm_open(&handle,playback_device_name,
			      SND_PCM_STREAM_PLAYBACK,mode)) < 0) {
	fprintf(stderr, "snd_pcm_open failed %s\n", snd_strerror(err));
	exit(1);
    }

    acast_clear_param(&iparam);
    // setup output parameters for sound card
    iparam.format = SND_PCM_FORMAT_S16_LE;
    iparam.sample_rate = 44100;
    iparam.channels_per_frame = num_output_channels;
    acast_setup_param(handle, &iparam, &oparam, &frames_per_packet);
    bytes_per_frame = oparam.bytes_per_channel*oparam.channels_per_frame;
    acast_print_params(stderr, &oparam);
    lparam = oparam;

    if ((sock=acast_receiver_open(multicast_addr,
				  multicast_ifaddr,
				  multicast_port,
				  &addr, &addrlen,
				  network_bufsize)) < 0) {
	fprintf(stderr, "unable to open multicast socket %s\n",
		strerror(errno));
	exit(1);
    }

    acast = (acast_t*) acast_buffer;
    silence =  (acast_t*) silence_buffer;
    silence->num_frames = frames_per_packet;
    snd_pcm_format_set_silence(oparam.format, silence->data,
			       frames_per_packet*bytes_per_frame);
    
    while(1) {
	int r;
	struct pollfd fds;

	fds.fd = sock;
	fds.events = POLLIN;

	if ((r = poll(&fds, 1, 1000)) == 1) {
	    r = recvfrom(sock, acast_buffer, sizeof(acast_buffer), 0, 
			 (struct sockaddr *) &addr, &addrlen);
	    if (r < 0) {
		perror("recvfrom");
		exit(1);
	    }
	    if (r == 0)
		continue;
#ifdef DEBUG
	    if ((r - sizeof(acast_t)) !=
		acast->param.bytes_per_channel *
		acast->param.channels_per_frame*acast->num_frames) {
		fprintf(stderr, "param data mismatch r=%d\n", r);
		acast_print(stderr, acast);
	    }
#endif
	    if (memcmp(&acast->param, &lparam, sizeof(acast_params_t)) != 0) {
		fprintf(stderr, "new parameters\n");
		acast_print(stderr, acast);

		snd_pcm_reset(handle);
		snd_pcm_prepare(handle);

		lparam = acast->param;
		iparam = lparam;
		iparam.channels_per_frame = num_output_channels;

		acast_setup_param(handle, &iparam, &oparam, &frames_per_packet);

		bytes_per_frame = oparam.bytes_per_channel*
		    oparam.channels_per_frame;
		silence->num_frames = frames_per_packet;
		snd_pcm_format_set_silence(oparam.format,
					   silence->data,
					   frames_per_packet*bytes_per_frame);
		acast_play(handle,bytes_per_frame,
			    silence->data,silence->num_frames);
		acast_play(handle,bytes_per_frame,
			   silence->data,silence->num_frames);
		snd_pcm_start(handle);
	    }
	    // rearrange data by select channels wanted
	    map_channels(oparam.format,
			 acast->data, acast->param.channels_per_frame,
			 snd_buffer, oparam.channels_per_frame,
			 channel_map,
			 acast->num_frames);

	    if (!seen_packet) {
		acast_play(handle,bytes_per_frame,
			    silence->data,silence->num_frames);
		acast_play(handle,bytes_per_frame,
			   silence->data,silence->num_frames);
		snd_pcm_start(handle);
	    }

	    if ((len=acast_play(handle,bytes_per_frame,snd_buffer,acast->num_frames)) < 0) {
		err = len;
		fprintf(stderr, "snd_pcm_writei %s\n", snd_strerror(err));
		snd_pcm_reset(handle);
		snd_pcm_prepare(handle);
		seen_packet = 0;
		continue;
	    }
	    if (len < acast->num_frames) {
		fprintf(stderr, "short write %d\n", len);
	    }

	    if (verbose && seen_packet &&
		((drop=(acast->seqno - last_seqno)) != 1))
		fprintf(stderr, "dropped %u frames\n", drop);
	    if ((verbose > 1) && (acast->seqno % 100) == 0) {
		acast_print(stderr, acast);
	    }
	    seen_packet = 1;
	    last_seqno = acast->seqno;
	}
    }
    exit(0);
}
