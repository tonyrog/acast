//
//  wav_sender
//
//     open a wav file and read data samples
//     and multicast over ip (ttl = 1)
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <endian.h>
#include <ctype.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <lame/lame.h>

#include "acast.h"
#include "tick.h"
#include "wav.h"


#define PLAYBACK_DEVICE "default"
// ttl=0 local host, ttl=1 local network
#define MULTICAST_TTL  1
#define MULTICAST_LOOP 0
#define NUM_CHANNELS   2

#define CHANNEL_MAP   "auto"
#define MAX_CHANNEL_OP  16
#define MAX_CHANNEL_MAP 8

#define MAX_U_32_NUM    0xFFFFFFFF

int verbose = 0;
int debug = 0;

#define min(a,b) (((a)<(b)) ? (a) : (b))
#define max(a,b) (((a)>(b)) ? (a) : (b))

void help(void)
{
printf("usage: wav_sender [options] file\n"
"  -h, --help      print help\n"
"  -v, --verbose   increase verbosity\n"
"  -D, --debug     debug verbosity\n"              
"  -a, --addr      multicast address (\"%s\")\n"
"  -i, --iface     multicast interface address (\"%s\")\n"
"  -p, --port      multicast address port (%d)\n"
"  -l, --loop      enable multi cast loop (%d)\n"
"  -t, --ttl       multicast ttl (%d)\n"
"  -d, --device    playback device (\"%s\")\n"
"  -c, --channels  number of output channels (%d)\n"
"  -m, --map       channel map (\"%s\")\n",       
       MULTICAST_ADDR,
       MULTICAST_IFADDR,
       MULTICAST_PORT,
       MULTICAST_LOOP,       
       MULTICAST_TTL,
       PLAYBACK_DEVICE,
       NUM_CHANNELS,
       CHANNEL_MAP);       
}


int main(int argc, char** argv)
{
    char* filename;
    acast_params_t mparam;
    snd_pcm_uframes_t wav_frames_per_packet;
    snd_pcm_uframes_t mcast_frames_per_packet;
    snd_pcm_uframes_t frames_per_packet;
    size_t mcast_bytes_per_frame;
    size_t bytes_per_frame;
    size_t bytes_to_send;
    uint32_t seqno = 0;
    int s;
    char* multicast_addr = MULTICAST_ADDR;
    char* multicast_ifaddr = MULTICAST_IFADDR;    // interface address
    uint16_t multicast_port = MULTICAST_PORT;
    int multicast_loop = MULTICAST_LOOP;   // loopback multicast packets
    int multicast_ttl  = MULTICAST_TTL;
    struct sockaddr_in addr;
    socklen_t addrlen;
    snd_pcm_format_t fmt;
    wav_header_t wav;
    xwav_header_t xwav;
    int fd, n, ret;
    uint32_t num_frames;
    int num_output_channels = 0;
    char* map = CHANNEL_MAP;
    acast_op_t channel_op[MAX_CHANNEL_OP];
    uint8_t    channel_map[MAX_CHANNEL_MAP];
    size_t     num_channel_ops;
    size_t     network_bufsize = 4*BYTES_PER_PACKET;
    tick_t     last_time;
    tick_t     first_time = 0;
    tick_t     send_time = 0;
    uint64_t   frame_delay_us;      // delay per frame in us
    uint64_t   sent_frames = 0;     // number of frames sent
    int map_type;
    
    while(1) {
	int option_index = 0;
	int c;
	static struct option long_options[] = {
	    {"help",   no_argument, 0,       'h'},
	    {"verbose",no_argument, 0,       'v'},
	    {"debug",  no_argument, 0,       'D'},
	    {"addr",   required_argument, 0, 'a'},
	    {"iface",  required_argument, 0, 'i'},
	    {"port",   required_argument, 0, 'p'},
	    {"ttl",    required_argument, 0, 't'},
	    {"loop",   no_argument, 0,       'l'},
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
    
    if ((ret = wav_decode_init(fd, &wav, &xwav, &num_frames)) < 0) {
	fprintf(stderr, "no wav data found\n");
	exit(1);
    }
    
    // max number of packets from wav format
    wav_frames_per_packet =
	wav_get_frames_per_buffer(&wav,BYTES_PER_PACKET - sizeof(acast_t));
	
    if ((map_type = parse_channel_map(map,
				      channel_op, MAX_CHANNEL_OP,
				      &num_channel_ops,
				      channel_map, MAX_CHANNEL_MAP,
				      wav.NumChannels,&num_output_channels))<0){
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

    if (verbose > 1) {
	wav_print(stderr, &wav);
	if (wav.AudioFormat == xwav.AudioFormat)
	    xwav_print(stderr, &xwav);
    }

    if ((fmt = wav_to_snd(wav.AudioFormat, wav.BitsPerChannel)) ==
	SND_PCM_FORMAT_UNKNOWN) {
	fprintf(stderr, "%04x unsupport wav file format\n", wav.AudioFormat);
	exit(1);
    }
    
    if ((s=acast_sender_open(multicast_addr,
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
    mparam.sample_rate = wav.SampleRate;
    mparam.channels_per_frame = num_output_channels;
    mparam.bits_per_channel = snd_pcm_format_width(fmt);
    mparam.bytes_per_channel = snd_pcm_format_physical_width(fmt) / 8;
    mcast_bytes_per_frame =
	mparam.bytes_per_channel*mparam.channels_per_frame;
    mcast_frames_per_packet = acast_get_frames_per_packet(&mparam);
    frame_delay_us = (mcast_frames_per_packet*1000000) / mparam.sample_rate;

    if (verbose) {
	fprintf(stderr, "mcast params:\n");
	acast_print_params(stderr, &mparam);
	fprintf(stderr, "  mcast_bytes_per_frame=%ld\n",
		mcast_bytes_per_frame);
	fprintf(stderr, "  mcast_frames_per_packet=%ld\n",
		mcast_frames_per_packet);
	fprintf(stderr, "----------------\n");
    }
    
    last_time = time_tick_now();

    frames_per_packet = min(mcast_frames_per_packet,wav_frames_per_packet);

    bytes_per_frame = num_output_channels * mparam.bytes_per_channel;

    if (verbose) {
	fprintf(stderr, "  num_output_channels:%d\n", num_output_channels);
	fprintf(stderr, "  bytes_per_frame:%ld\n",  bytes_per_frame);
	fprintf(stderr, "  frames_per_packet:%ld\n", frames_per_packet);
    }

    // fixme we probably need cast_buffer and play_buffer!!!!
	
    while((num_frames==MAX_U_32_NUM) || (num_frames >= frames_per_packet)) {
	char src_buffer[BYTES_PER_PACKET];
	acast_t* src;
	char dst_buffer[BYTES_PER_PACKET];
	acast_t* dst;

	src = (acast_t*) src_buffer;
	if ((n = wav_decode(fd,src->data,&wav,frames_per_packet)) < 0) {
	    fprintf(stderr, "read error: %s\n", strerror(errno));
	    exit(1);
	}

	switch(map_type) {
	case 1:
	    dst = (acast_t*) dst_buffer;
	    dst->param = mparam;
	    map_channels(mparam.format,
			 src->data, wav.NumChannels,
			 dst->data, num_output_channels, 
			 channel_map,
			 frames_per_packet);
	    break;
	case 2:
	    dst = (acast_t*) dst_buffer;
	    dst->param = mparam;
	    op_channels(mparam.format,
			src->data, wav.NumChannels,
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
    
	if (num_frames < MAX_U_32_NUM)
	    num_frames -= n;
	dst->seqno = seqno++;
	dst->num_frames = n;
	if ((verbose > 3) && (dst->seqno % 100 == 0)) {
	    acast_print(stderr, dst);
	}
	bytes_to_send = bytes_per_frame*dst->num_frames;
	if (sendto(s, (void*)dst, sizeof(acast_t)+bytes_to_send, 0,
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
	}
	last_time = time_tick_wait_until(send_time + frame_delay_us);
	// send_time is the absolute send time mark
	send_time += frame_delay_us;
    }
    exit(0);
}
