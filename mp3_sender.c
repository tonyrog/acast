//
//  afile_sender
//
//   open an audio file and send samples over udp/unicast/multicast (ttl=1)
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "acast.h"
#include "acast_file.h"
#include "tick.h"

// ttl=0 local host, ttl=1 local network
#define MULTICAST_TTL  1
#define MULTICAST_LOOP 0
#define NUM_CHANNELS   0
#define CHANNEL_MAP   "auto"

#define PCM_BUFFER_SIZE 1152
#define BYTES_PER_BUFFER ((PCM_BUFFER_SIZE*2*2)+sizeof(acast_t))

int verbose = 0;
int debug = 0;


void help(void)
{
printf("usage: afile_sender [options] file\n"
"  -h, --help      print help\n"
"  -v, --verbose   increase verbosity\n"
"  -D, --debug     debug verbosity\n"              
"  -a, --maddr     multicast address (\"%s\")\n"
"  -i, --iface     multicast interface address (\"%s\")\n"
"  -u, --uaddr     unicast address (NULL)\n"       
"  -p, --port      multicast address port (%d)\n"
"  -l, --loop      enable multi cast loop (%d)\n"
"  -t, --ttl       multicast ttl (%d)\n"
"  -c, --channels  number of output channels (%d)\n"
"  -m, --map       channel map (\"%s\")\n",       
       MULTICAST_ADDR,
       INTERFACE_ADDR,
       MULTICAST_PORT,
       MULTICAST_LOOP,       
       MULTICAST_TTL,
       NUM_CHANNELS,
       CHANNEL_MAP);       
}

int main(int argc, char** argv)
{
    char* filename;
    acast_params_t mparam;
    snd_pcm_uframes_t af_frames_per_packet;    
    snd_pcm_uframes_t frames_per_packet;    
    uint32_t seqno = 0;
    int sock;
    char* multicast_addr = MULTICAST_ADDR;
    uint16_t multicast_port = MULTICAST_PORT;
    int multicast_loop = MULTICAST_LOOP;   // loopback multicast packets
    int multicast_ttl  = MULTICAST_TTL;
    char* interface_addr = INTERFACE_ADDR;    // interface address
    char* unicast_addr   = NULL;    
    struct sockaddr_in addr;
    socklen_t addrlen;
    uint8_t src_buffer[BYTES_PER_BUFFER];
    uint8_t dst_buffer[2*BYTES_PER_BUFFER];
    uint8_t* dst_ptr;
    int frames_remain;
    int num_frames;
    int num_output_channels = NUM_CHANNELS;
    char* map = CHANNEL_MAP;
    acast_channel_ctx_t chan_ctx;
    size_t bytes_per_frame;
    size_t network_bufsize = 4*BYTES_PER_PACKET;
    tick_t last_time;
    tick_t first_time = 0;
    tick_t send_time = 0;
    uint64_t frame_delay_us;  // delay per frame in us
    uint64_t sent_frames;     // number of frames sent
    acast_file_t* af;
    acast_buffer_t abuf;
    
    while(1) {
	int option_index = 0;
	int c;
	static struct option long_options[] = {
	    {"help",   no_argument, 0,        'h'},
	    {"verbose",no_argument, 0,        'v'},
	    {"debug",  no_argument, 0,        'D'},
	    {"maddr",   required_argument, 0, 'a'},
	    {"iface",  required_argument, 0,  'i'},
	    {"uaddr",  required_argument, 0,  'u'},	    
	    {"port",   required_argument, 0,  'p'},
	    {"ttl",    required_argument, 0,  't'},
	    {"loop",   no_argument, 0,        'l'},
	    {"device", required_argument, 0,  'd'},
	    {"channels",required_argument, 0, 'c'},
	    {"map",     required_argument, 0, 'm'},	    
	    {0,        0,                 0, 0}
	};
	
	c = getopt_long(argc, argv, "lhvDa:u:i:p:t:c:m:",
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
	case 'a':
	    multicast_addr = strdup(optarg);
	    break;
	case 'i':
	    interface_addr = strdup(optarg);
	    break;
	case 'u':
	    unicast_addr = strdup(optarg);
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
	case 'm':
	    map = strdup(optarg);
	    break;
	default:
	    help();
	    exit(1);
	}
    }
    
    if (optind >= argc) {
	help();
	exit(1);
    }

    time_tick_init();    
    
    filename = argv[optind];
    if ((af = acast_file_open(filename, O_RDONLY)) == NULL) {
	fprintf(stderr, "error: unable to open %s: %s\n",
		filename, strerror(errno));
	exit(1);
    }

    af_frames_per_packet =
	acast_file_frames_per_buffer(af,BYTES_PER_PACKET-sizeof(acast_t));    

    if (verbose > 1) {
	acast_file_print(af, stderr);
	fprintf(stderr, "  af_frames_per_packet = %lu\n",
		af_frames_per_packet);
    }

    if (parse_channel_ctx(map,&chan_ctx,af->param.channels_per_frame,
			  &num_output_channels) < 0) {
	fprintf(stderr, "map synatx error\n");
	exit(1);
    }
    
    if (verbose) {
	print_channel_ctx(stdout, &chan_ctx);
	printf("num_output_channels = %d\n", num_output_channels);
    }    

    if (af->param.format == SND_PCM_FORMAT_UNKNOWN) {
	fprintf(stderr, "unsupport audio format\n");
	exit(1);
    }
    
    sent_frames = 0;

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

    mparam = af->param;
    mparam.channels_per_frame = num_output_channels;
    
    bytes_per_frame = mparam.bytes_per_channel*mparam.channels_per_frame;
    frames_per_packet = acast_get_frames_per_packet(&mparam);
    frame_delay_us = (frames_per_packet*1000000) / mparam.sample_rate;
    
    if (verbose > 1) {
	acast_print_params(stderr, &mparam);
	fprintf(stderr, "frames_per_packet=%ld\n", frames_per_packet);
    }
	
    frames_remain = 0;  // samples that remain from last round
    dst_ptr = dst_buffer;
    
    last_time = time_tick_now();
    
    while((num_frames = acast_file_read(af, &abuf, src_buffer,
					BYTES_PER_BUFFER-sizeof(acast_t),
					frames_per_packet)) > 0) {

	// convert all frames 
	switch(chan_ctx.type) {
	case ACAST_MAP_ID:	
	case ACAST_MAP_PERMUTE:
	    permute_ni(mparam.format,
		       abuf.data, abuf.stride, abuf.size,
		       dst_ptr, num_output_channels,
		       chan_ctx.channel_map,
		       num_frames);
	    break;
	case ACAST_MAP_OP:
	    scatter_gather_ni(mparam.format,
			      abuf.data, abuf.stride, abuf.size,
			      dst_ptr, num_output_channels,
			      chan_ctx.channel_op, chan_ctx.num_channel_ops,
			      num_frames);
	    break;
	default:
	    fprintf(stderr, "bad ctx type %d\n", chan_ctx.type);
	    exit(1);		
	}
	
	num_frames += frames_remain;

	dst_ptr = dst_buffer;

	while(num_frames >= frames_per_packet) {
	    uint8_t packet_buffer[BYTES_PER_PACKET];
	    acast_t* packet;
	    size_t  bytes_to_send;

	    packet = (acast_t*) packet_buffer;
	    packet->seqno = seqno++;
	    packet->num_frames = frames_per_packet;

	    bytes_to_send = frames_per_packet*bytes_per_frame;
	    memcpy(packet->data, dst_ptr, bytes_to_send);
	    dst_ptr += bytes_to_send;
	    
	    if ((verbose > 3) && (seqno % 100 == 0)) {
		acast_print(stderr, packet);
	    }

	    if (sendto(sock, (void*)packet, sizeof(acast_t)+bytes_to_send, 0,
		       (struct sockaddr *) &addr, addrlen) < 0) {
		fprintf(stderr, "failed to send frame %s\n",
			strerror(errno));
	    }
	    else {
		sent_frames++;
		if ((sent_frames & 0xff) == 0) {
		    if (verbose > 1) {
			fprintf(stderr, "SEND RATE = %ldHz, %.2fMb/s\n",
				(1000000*sent_frames*frames_per_packet)/
				(last_time-first_time),
				
				((1000000*sent_frames*8*BYTES_PER_PACKET)/
				 (double)(last_time-first_time)) /
				(double)(1024*1024));
		    }
		}
		if (sent_frames == 1) {
		    first_time = last_time;
		    send_time = first_time;
		}
		last_time = time_tick_wait_until(send_time + frame_delay_us);
		// send_time is the absolute send time mark
		send_time += frame_delay_us;
		num_frames -= frames_per_packet;
	    }
	}
	memcpy(dst_buffer, dst_ptr, num_frames*bytes_per_frame);
	frames_remain = num_frames;
    }
    exit(0);
}
