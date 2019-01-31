//
//  mp3_sender
//
//     open a mp3 file and read data samples
//     and multicast over ip (ttl = 1)
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

#include <lame/lame.h>

#include "acast.h"
#include "tick.h"
#include "mp3.h"

// ttl=0 local host, ttl=1 local network
#define MULTICAST_TTL  1
#define MULTICAST_LOOP 0
#define NUM_CHANNELS   0

#define CHANNEL_MAP   "auto"
#define MAX_CHANNEL_OP  16
#define MAX_CHANNEL_MAP 8

#define MAX_U_32_NUM    0xFFFFFFFF
#define MP3BUFFER_SIZE  4096
#define MP3TAG_SIZE     4096
#define PCM_BUFFER_SIZE 1152

int in_id3v2_size = -1;
uint8_t* in_id3v2_tag = NULL;

int verbose = 0;
int debug = 0;

void emit_message(const char* format, va_list ap)
{
    vfprintf(stderr, format, ap);
}

void emit_error(const char* format, va_list ap)
{
    vfprintf(stderr, format, ap);
}

void emit_debug(const char* format, va_list ap)
{
    vfprintf(stderr, format, ap);
}

void help(void)
{
printf("usage: mp3_sender [options] file\n"
"  -h, --help      print help\n"
"  -v, --verbose   increase verbosity\n"
"  -D, --debug     debug verbosity\n"              
"  -a, --addr      multicast address (\"%s\")\n"
"  -i, --iface     multicast interface address (\"%s\")\n"
"  -p, --port      multicast address port (%d)\n"
"  -l, --loop      enable multi cast loop (%d)\n"
"  -t, --ttl       multicast ttl (%d)\n"
"  -c, --channels  number of output channels (%d)\n"
"  -m, --map       channel map (\"%s\")\n",       
       MULTICAST_ADDR,
       MULTICAST_IFADDR,
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
    snd_pcm_uframes_t frames_per_packet;    
    uint32_t seqno = 0;
    int sock;
    char* multicast_addr = MULTICAST_ADDR;
    char* multicast_ifaddr = MULTICAST_IFADDR;    // interface address
    uint16_t multicast_port = MULTICAST_PORT;
    int multicast_loop = MULTICAST_LOOP;   // loopback multicast packets
    int multicast_ttl  = MULTICAST_TTL;
    struct sockaddr_in addr;
    socklen_t addrlen;
    hip_t hip;
    snd_pcm_format_t fmt;    
    mp3data_struct mp3;
    int16_t pcm_l[2*PCM_BUFFER_SIZE];
    int16_t pcm_r[2*PCM_BUFFER_SIZE];
    int pcm_remain;
    int fd;
    int num_frames;
    int enc_delay, enc_padding;
    int num_output_channels = NUM_CHANNELS;
    char* map = CHANNEL_MAP;
    acast_op_t channel_op[MAX_CHANNEL_OP];
    uint8_t    channel_map[MAX_CHANNEL_MAP];
    size_t num_channel_ops;    
    size_t bytes_per_frame;
    size_t bytes_to_send;    
    size_t network_bufsize = 4*BYTES_PER_PACKET;
    tick_t last_time;
    tick_t first_time = 0;
    tick_t send_time = 0;
    uint64_t frame_delay_us;  // delay per frame in us
    uint64_t sent_frames;     // number of frames sent
    int map_type;
    
    while(1) {
	int option_index = 0;
	int c;
	static struct option long_options[] = {
	    {"help",   no_argument, 0,        'h'},
	    {"verbose",no_argument, 0,        'v'},
	    {"debug",  no_argument, 0,        'D'},
	    {"addr",   required_argument, 0,  'a'},
	    {"iface",  required_argument, 0,  'i'},
	    {"port",   required_argument, 0,  'p'},
	    {"ttl",    required_argument, 0,  't'},
	    {"loop",   no_argument, 0,        'l'},
	    {"device", required_argument, 0,  'd'},
	    {"channels",required_argument, 0, 'c'},
	    {"map",     required_argument, 0, 'm'},	    
	    {0,        0,                 0, 0}
	};
	
	c = getopt_long(argc, argv, "lhvDa:i:p:t:c:m:",
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
    
    filename = argv[optind];
    if ((fd = open(filename, O_RDONLY)) < 0) {
	fprintf(stderr, "error: unable to open %s: %s\n",
		filename, strerror(errno));
	exit(1);
    }

    time_tick_init();
    
    if ((hip = hip_decode_init()) == NULL) {
	perror("hip_decode_init");
	exit(1);
    }

    hip_set_msgf(hip, verbose ? emit_message : 0);
    hip_set_errorf(hip, verbose ? emit_error : 0);
    hip_set_debugf(hip, emit_debug);

    if (mp3_decode_init(fd, hip, &mp3, &enc_delay, &enc_padding) < 0) {
	fprintf(stderr, "failed detect mp3 file format\n");
	exit(1);
    }

    if ((map_type = parse_channel_map(map,
				      channel_op, MAX_CHANNEL_OP,
				      &num_channel_ops,
				      channel_map, MAX_CHANNEL_MAP,
				      mp3.stereo,&num_output_channels))<0) {
	fprintf(stderr, "map synatx error\n");
	exit(1);
    }
    
    if (verbose) {
	printf("Channel map: ");
	print_channel_ops(channel_op, num_channel_ops);
	printf("use_channel_map: %d\n", (map_type > 0));
	printf("id_channel_map: %d\n",  (map_type == 1));
	printf("num_output_channels = %d\n", num_output_channels);
    }    
    
    if (verbose > 1) {
	mp3_print(stderr, &mp3);
	fprintf(stderr, "enc_delay=%d\n", enc_delay);
	fprintf(stderr, "enc_padding=%d\n", enc_padding);
    }

    fmt = SND_PCM_FORMAT_S16_LE;
    sent_frames = 0;

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
    mparam.format = fmt;
    mparam.sample_rate = mp3.samplerate;
    mparam.channels_per_frame = num_output_channels;
    mparam.bits_per_channel = snd_pcm_format_width(fmt);
    mparam.bytes_per_channel = snd_pcm_format_physical_width(fmt) / 8;
    bytes_per_frame = mparam.bytes_per_channel*mparam.channels_per_frame;
    frames_per_packet = acast_get_frames_per_packet(&mparam);
    frame_delay_us = (frames_per_packet*1000000) / mparam.sample_rate;
    
    if (verbose > 1) {
	acast_print_params(stderr, &mparam);
	fprintf(stderr, "frames_per_packet=%ld\n", frames_per_packet);
    }
    
    pcm_remain = 0;  // samples that remain from last round

    last_time = time_tick_now();
    
    while((num_frames = mp3_decode(fd, hip,
				   &pcm_l[pcm_remain],
				   &pcm_r[pcm_remain], &mp3)) > 0) {
	void* channels[2];
	int16_t *lptr = pcm_l;
	int16_t *rptr = pcm_r;
	char dst_buffer[BYTES_PER_PACKET];
	acast_t* dst;	

	num_frames += pcm_remain;

	dst = (acast_t*) dst_buffer;
	dst->param = mparam;
	
	while(num_frames >= frames_per_packet) {
	    channels[0] = (void*) lptr;
	    channels[1] = (void*) rptr;

	    switch(map_type) {
	    case 0:
	    case 1:
		imap_channels(mparam.format,
			      channels, dst->param.channels_per_frame,
			      dst->data, channel_map,
			      frames_per_packet);
		break;
	    case 2:
		iop_channels(mparam.format,
			     channels, 2,
			     dst->data, num_output_channels,
			     channel_op, num_channel_ops,
			     frames_per_packet);
		break;
	    }
		
	    lptr += frames_per_packet;
	    rptr += frames_per_packet;	    
	    
	    dst->seqno = seqno++;
	    dst->num_frames = frames_per_packet;
	    
	    if ((verbose > 3) && (seqno % 100 == 0)) {
		acast_print(stderr, dst);
	    }

	    bytes_to_send = dst->num_frames*bytes_per_frame;
	    if (sendto(sock, (void*)dst, sizeof(acast_t)+bytes_to_send, 0,
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
		last_time = time_tick_wait_until(send_time +
						 frame_delay_us-enc_delay);
		// send_time is the absolute send time mark
		send_time += frame_delay_us;
		num_frames -= frames_per_packet;
	    }
	}
	if (num_frames) {
	    if (lptr == pcm_l) { // read short
		pcm_remain += num_frames;
	    }
	    else {                        // read long
		memcpy(pcm_l, lptr, num_frames*sizeof(int16_t));
		memcpy(pcm_r, rptr, num_frames*sizeof(int16_t));
		pcm_remain = num_frames;
	    }
	}
	else {
	    pcm_remain = 0;
	}
    }
    hip_decode_exit(hip);
    exit(0);
}
