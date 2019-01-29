//
//  acast_sender
//
//     open a sound device and read data samples
//     and multicast over ip (ttl = 1)
//
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <getopt.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "acast.h"

#define CAPTURE_DEVICE "default"
#define NUM_CHANNELS  6
#define CHANNEL_MAP   "012345"

// ttl=0 local host, ttl=1 local network
#define MULTICAST_TTL 1
#define MULTICAST_LOOP 0

void help(void)
{
printf("usage: acast_sender [options]\n"
"  -h, --help      print help\n"
"  -v, --verbose   increase verbosity\n"
"  -a, --addr      multicast address (%s)\n"
"  -i, --iface     multicast interface address (%s)\n"
"  -p, --port      multicast address port (%d)\n"
"  -l, --loop      enable multi cast loop (%d)\n"
"  -t, --ttl       multicast ttl (%d)\n"
"  -d, --device    capture device (%s)\n"
"  -c, --channels  number of output channels (%d)\n"
"  -m, --map       channel map (%s)\n",
       MULTICAST_ADDR,
       MULTICAST_IFADDR,
       MULTICAST_PORT,
       MULTICAST_LOOP,       
       MULTICAST_TTL,
       CAPTURE_DEVICE,
       NUM_CHANNELS,
       CHANNEL_MAP);
}

int verbose = 0;

int main(int argc, char** argv)
{
    char* capture_device_name = CAPTURE_DEVICE;
    snd_pcm_t *handle;
    acast_params_t iparam;
    acast_params_t oparam;
    uint32_t seqno = 0;
    snd_pcm_uframes_t frames_per_packet = 0;
    int err;
    int sock;
    char acast_buffer[BYTES_PER_PACKET];
    acast_t* acast;
    char* multicast_addr = MULTICAST_ADDR;
    char* multicast_ifaddr = MULTICAST_IFADDR; // interface address
    uint16_t multicast_port = MULTICAST_PORT;    
    int multicast_loop = MULTICAST_LOOP;       // loopback multicast packets
    int multicast_ttl  = MULTICAST_TTL;
    int num_output_channels = NUM_CHANNELS;
    uint8_t channel_map[8] = {0,1,0,1,0,0,0,0};    
    struct sockaddr_in addr;
    socklen_t addrlen;
    size_t bytes_per_frame;
    size_t network_bufsize = 2*BYTES_PER_PACKET;
    int mode = 0; // SND_PCM_NONBLOCK;

    while(1) {
	int option_index = 0;
	int c;
	static struct option long_options[] = {
	    {"help",   no_argument, 0,       'h'},
	    {"verbose",no_argument, 0,       'v'},
	    {"addr",   required_argument, 0, 'a'},
	    {"iface",  required_argument, 0, 'i'},
	    {"port",   required_argument, 0, 'p'},
	    {"ttl",    required_argument, 0, 't'},
	    {"loop",   no_argument, 0,       'l'},
	    {"device", required_argument, 0, 'd'},
	    {"channels",required_argument, 0, 'c'},
	    {"map",     required_argument, 0, 'm'},
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
	case 'l':
	    multicast_loop = 1;
	    break;
	case 't':
	    multicast_ttl = atoi(optarg);
	    break;	    
	case 'd':
	    capture_device_name = strdup(optarg);
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
	case 'c':
	    num_output_channels = atoi(optarg);
	    break;
	case 'm': {
	    char* ptr = optarg;
	    int i = 0;
	    while((i < 8) && isdigit(*ptr)) {
		channel_map[i++] = (*ptr-'0');
	    }
	    break;
	}
	default:
	    help();
	    exit(1);
	}
    }

    if ((err = snd_pcm_open(&handle, capture_device_name,
			    SND_PCM_STREAM_CAPTURE, mode)) < 0) {
	fprintf(stderr, "snd_pcm_open failed %s\n", snd_strerror(err));
	exit(1);
    }
    acast_clear_param(&iparam);

    // setup wanted paramters
    iparam.format = SND_PCM_FORMAT_S16_LE;
    iparam.sample_rate = 22000;
    iparam.channels_per_frame = 6;
    acast_setup_param(handle, &iparam, &oparam, &frames_per_packet);
    bytes_per_frame = oparam.bytes_per_channel * oparam.channels_per_frame;

    // fill in "constant" values in the acast header
    acast = (acast_t*) acast_buffer;
    acast->seqno       = seqno;
    acast->num_frames  = 0;
    acast->param       = oparam;

    acast_print(stderr, acast);

    if ((sock = acast_sender_open(multicast_addr,
				  multicast_ifaddr,
				  multicast_port,
				  multicast_ttl,
				  multicast_loop,
				  &addr, &addrlen,
				  network_bufsize)) < 0) {
	fprintf(stderr, "unable to open multicast socket %s\n",
		strerror(errno));
	exit(1);
    }
    
    while(1) {
	int r;

	if ((r = acast_record(handle, bytes_per_frame,
			      acast->data, frames_per_packet)) < 0) {
	    fprintf(stderr, "acast_read failed: %s\n", snd_strerror(r));
	    exit(1);
	}
	else if (r == 0) {
	    fprintf(stderr, "acast_read read zero bytes\n");
	}
	else {
	    size_t nbytes = r * bytes_per_frame;
	    
	    if (r < frames_per_packet)
		fprintf(stderr, "acast_read read shortro bytes\n");
	    acast->seqno = seqno++;
	    acast->num_frames = r;
	    if (acast->seqno % 100 == 0)
		acast_print(stderr, acast);
	    sendto(sock, acast_buffer, sizeof(acast_t)+nbytes, 0,
		   (struct sockaddr *) &addr, addrlen);
	}
    }
    exit(0);
}
