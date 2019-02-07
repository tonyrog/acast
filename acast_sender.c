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
#include "tick.h"

#define CAPTURE_DEVICE "default"
#define NUM_CHANNELS  6

#define CHANNEL_MAP   "auto"
#define MAX_CHANNEL_OP  16
#define MAX_CHANNEL_MAP 8

// ttl=0 local host, ttl=1 local network
#define MULTICAST_TTL  1
#define MULTICAST_LOOP 0

#define min(a,b) (((a)<(b)) ? (a) : (b))
#define max(a,b) (((a)>(b)) ? (a) : (b))


void help(void)
{
printf("usage: acast_sender [options]\n"
"  -h, --help      print help\n"
"  -v, --verbose   increase verbosity\n"
"  -D, --debug     debug verbosity\n"    
"  -a, --addr      multicast address (%s)\n"
"  -i, --iface     multicast interface address (%s)\n"
"  -p, --port      multicast address port (%d)\n"
"  -l, --loop      enable multi cast loop (%d)\n"
"  -t, --ttl       multicast ttl (%d)\n"
"  -d, --device    capture device (%s)\n"
"  -c, --channels  number of output channels (%d)\n"
"  -C, --ichannels  number of input channels (%d)\n"
"  -m, --map       channel map (%s)\n",
       MULTICAST_ADDR,
       INTERFACE_ADDR,
       MULTICAST_PORT,
       MULTICAST_LOOP,       
       MULTICAST_TTL,
       CAPTURE_DEVICE,
       NUM_CHANNELS,
       NUM_CHANNELS,       
       CHANNEL_MAP);
}

int verbose = 0;
int debug = 0;

int main(int argc, char** argv)
{
    char* capture_device_name = CAPTURE_DEVICE;
    snd_pcm_t *handle;
    acast_params_t iparam;
    acast_params_t sparam;
    acast_params_t mparam;    
    uint32_t seqno = 0;
    snd_pcm_uframes_t snd_frames_per_packet = 0;
    snd_pcm_uframes_t mcast_frames_per_packet = 0;        
    snd_pcm_uframes_t frames_per_packet = 0;
    size_t mcast_bytes_per_frame;
    size_t bytes_per_frame;
    int err;
    int sock;
    char src_buffer[BYTES_PER_PACKET];
    acast_t* src;
    char* multicast_addr = MULTICAST_ADDR;
    uint16_t multicast_port = MULTICAST_PORT;    
    int multicast_loop = MULTICAST_LOOP;       // loopback multicast packets
    int multicast_ttl  = MULTICAST_TTL;
    char* interface_addr = INTERFACE_ADDR; // interface address
    char* unicast_addr   = NULL;
    int num_output_channels = 0;
    int num_input_channels = 2;
    char* map = CHANNEL_MAP;
    acast_op_t channel_op[MAX_CHANNEL_OP];
    uint8_t    channel_map[MAX_CHANNEL_MAP];
    size_t     num_channel_ops;    
    struct sockaddr_in addr;
    socklen_t addrlen;
    size_t bytes_to_send;    
    size_t network_bufsize = 2*BYTES_PER_PACKET;
    tick_t     last_time;
    tick_t     first_time = 0;
    uint64_t   sent_frames = 0;
    int mode = 0; // SND_PCM_NONBLOCK;
    int map_type;
    
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
	    {"ichannels",required_argument, 0, 'C'},	    
	    {"map",     required_argument, 0, 'm'},
	    {0,        0,                 0, 0}
	};
	
	c = getopt_long(argc, argv, "lhvDa:s:i:p:t:d:c:C:",
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
	case 'D':
	    verbose++;
	    debug = 1;
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
	case 's':
	    unicast_addr = strdup(optarg);
	    break;	    
	case 'a':
	    multicast_addr = strdup(optarg);
	    break;
	case 'i':
	    interface_addr = strdup(optarg);
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
	case 'C':
	    num_input_channels = atoi(optarg);
	    break;	    
	case 'm':
	    map = strdup(optarg);
	    break;
	default:
	    help();
	    exit(1);
	}
    }

    time_tick_init();    

    if ((err = snd_pcm_open(&handle, capture_device_name,
			    SND_PCM_STREAM_CAPTURE, mode)) < 0) {
	fprintf(stderr, "snd_pcm_open failed %s\n", snd_strerror(err));
	exit(1);
    }
    
    acast_clear_param(&iparam);
    // setup wanted paramters
    iparam.format = SND_PCM_FORMAT_S16_LE;
    iparam.sample_rate = 48000;
    iparam.channels_per_frame = num_input_channels;
    acast_setup_param(handle, &iparam, &sparam, &snd_frames_per_packet);
    bytes_per_frame = sparam.bytes_per_channel * sparam.channels_per_frame;

    if ((map_type = parse_channel_map(map,
				      channel_op, MAX_CHANNEL_OP,
				      &num_channel_ops,
				      channel_map, MAX_CHANNEL_MAP,
				      sparam.channels_per_frame,
				      &num_output_channels)) < 0) {
	fprintf(stderr, "map synatx error\n");
	exit(1);
    }

    if (verbose) {
	printf("Channel map: ");
	print_channel_ops(channel_op, num_channel_ops);
	printf("use_channel_map: %d\n", (map_type>0));
	printf("id_channel_map: %d\n",  (map_type==1));
	printf("num_output_channels = %d\n", num_output_channels);
    }

    mparam = sparam;
    mparam.channels_per_frame = num_output_channels;
    mcast_bytes_per_frame =
	mparam.bytes_per_channel*mparam.channels_per_frame;    
    mcast_frames_per_packet = acast_get_frames_per_packet(&mparam);

    if (verbose) {
	fprintf(stderr, "mcast params:\n");
	acast_print_params(stderr, &mparam);
	fprintf(stderr, "  mcast_bytes_per_frame=%ld\n",
		mcast_bytes_per_frame);
	fprintf(stderr, "  mcast_frames_per_packet=%ld\n",
		mcast_frames_per_packet);
	fprintf(stderr, "----------------\n");
    }
    
    
    // fill in "constant" values in the acast header
    src = (acast_t*) src_buffer;
    src->seqno       = seqno;
    src->num_frames  = 0;
    src->param       = sparam;

    acast_print(stderr, src);

    if (unicast_addr != NULL) {
	if ((sock = acast_usender_open(unicast_addr, interface_addr,
				       multicast_port, 
				       &addr, &addrlen,
				       network_bufsize)) < 0) {
	    fprintf(stderr, "unable to open unicast socket %s\n",
		    strerror(errno));
	    exit(1);
	}
    }
    else {
	if ((sock = acast_sender_open(multicast_addr,
				      interface_addr,
				      multicast_port,
				      multicast_ttl,
				      multicast_loop,
				      &addr, &addrlen,
				      network_bufsize)) < 0) {
	    fprintf(stderr, "unable to open multicast socket %s\n",
		    strerror(errno));
	    exit(1);
	}
    }
    if (verbose) {
	fprintf(stderr, "multicast to %s:%d\n",
		multicast_addr, multicast_port);
	fprintf(stderr, "send from interface %s ttl=%d loop=%d\n",
		interface_addr, multicast_ttl,  multicast_loop);
	fprintf(stderr, "send to addr=%s, port=%d, len=%d\n",
		inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), addrlen);
    }

    frames_per_packet = min(mcast_frames_per_packet,snd_frames_per_packet);
    bytes_per_frame = num_output_channels * mparam.bytes_per_channel;

    last_time = time_tick_now();
    first_time = time_tick_now();
    
    while(1) {
	int r;

	if ((r = acast_record(handle, bytes_per_frame,
			      src->data, frames_per_packet)) < 0) {
	    fprintf(stderr, "acast_read failed: %s\n", snd_strerror(r));
	    exit(1);
	}
	else if (r == 0) {
	    fprintf(stderr, "acast_read read zero bytes\n");
	}
	else {
	    char dst_buffer[BYTES_PER_PACKET];
	    acast_t* dst;
	    
	    if (r < frames_per_packet)
		fprintf(stderr, "acast_read read short bytes\n");
	    
	    switch(map_type) {
	    case 1:
		dst = (acast_t*) dst_buffer;
		dst->param = mparam;
		map_channels(mparam.format,
			     src->data, sparam.channels_per_frame,
			     dst->data, num_output_channels, 
			     channel_map,
			     frames_per_packet);
		break;
	    case 2:
		dst = (acast_t*) dst_buffer;
		dst->param = mparam;
		op_channels(mparam.format,
			    src->data, sparam.channels_per_frame,
			    dst->data, num_output_channels,
			    channel_op, num_channel_ops,
			    frames_per_packet);
		break;
	    case 0:
	    default:
		dst = src;
		dst->param = mparam;
		break;
	    }
	    dst->seqno = seqno++;
	    dst->num_frames = r;
	    bytes_to_send = bytes_per_frame*dst->num_frames;
	    if (sendto(sock, (void*)dst, sizeof(acast_t)+bytes_to_send, 0,
		       (struct sockaddr *) &addr, addrlen) < 0) {
		fprintf(stderr, "failed to send frame %s\n",
			strerror(errno));
	    }
	    else {
		sent_frames++;
		if ((sent_frames & 0xff) == 0) {
		    if (verbose > 1) {
			fprintf(stderr, "SEND RATE = %.2fKHz, %.2fMb/s\n",
				(1000*sent_frames*frames_per_packet)/
				((double)(last_time-first_time)),
				((1000000*sent_frames*8*BYTES_PER_PACKET)/
				 (double)(last_time-first_time)) /
				(double)(1024*1024));
		    }
		    if (verbose > 3)
			acast_print(stderr, dst);
		}
		last_time = time_tick_now();
	    }
	}
    }
    exit(0);
}
