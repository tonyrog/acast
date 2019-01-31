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

#define NUM_CHANNELS  0

#define CHANNEL_MAP   "auto"
#define MAX_CHANNEL_OP  16
#define MAX_CHANNEL_MAP 8

#define SRC_CHANNELS 6  // max number of channels supported

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
       INTERFACE_ADDR,
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
    char* multicast_ifaddr = INTERFACE_ADDR;    // interface address
    uint16_t multicast_port = MULTICAST_PORT;
    struct sockaddr_in addr;
    socklen_t addrlen;
    uint8_t silence_buffer[BYTES_PER_PACKET];
    acast_t* silence;
    uint32_t last_seqno = 0;
    acast_params_t iparam;
    acast_params_t sparam;
    acast_params_t lparam;
    snd_pcm_uframes_t frames_per_packet;
    uint32_t drop = 0;
    uint32_t seen_packet = 0;
    int num_output_channels = NUM_CHANNELS;
    char* map = CHANNEL_MAP;
    acast_op_t channel_op[MAX_CHANNEL_OP];
    uint8_t    channel_map[MAX_CHANNEL_MAP];
    size_t num_channel_ops;
    size_t bytes_per_frame;
    size_t network_bufsize = BYTES_PER_PACKET;
    int mode = SND_PCM_NONBLOCK;
    int map_type;
    size_t flushed_packets = 0;
    int flushing;
    
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
	    {"device", required_argument, 0,  'd'},
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
	case 'D':
	    verbose++;
	    debug = 1;
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

    if ((err=snd_pcm_open(&handle,playback_device_name,
			      SND_PCM_STREAM_PLAYBACK,mode)) < 0) {
	fprintf(stderr, "snd_pcm_open failed %s\n", snd_strerror(err));
	exit(1);
    }

    if ((map_type = parse_channel_map(map,
				      channel_op, MAX_CHANNEL_OP,
				      &num_channel_ops,
				      channel_map, MAX_CHANNEL_MAP,
				      SRC_CHANNELS,&num_output_channels))<0) {
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
    
    acast_clear_param(&iparam);
    // setup output parameters for sound card
    iparam.format = SND_PCM_FORMAT_S16_LE;
    iparam.sample_rate = 44100;
    iparam.channels_per_frame = num_output_channels;
    acast_setup_param(handle, &iparam, &sparam, &frames_per_packet);
    bytes_per_frame = sparam.bytes_per_channel*sparam.channels_per_frame;
    acast_print_params(stderr, &sparam);
    lparam = sparam;

    if ((sock=acast_receiver_open(multicast_addr,
				  multicast_ifaddr,
				  multicast_port,
				  &addr, &addrlen,
				  network_bufsize)) < 0) {
	fprintf(stderr, "unable to open multicast socket %s\n",
		strerror(errno));
	exit(1);
    }
    if (verbose) {
	fprintf(stderr, "multicast from %s:%d on interface %s\n",
		multicast_addr, multicast_port, multicast_ifaddr);
	fprintf(stderr, "recv addr=%s, len=%d\n",
		inet_ntoa(addr.sin_addr), addrlen);
    }
    

    silence =  (acast_t*) silence_buffer;
    silence->num_frames = frames_per_packet;
    snd_pcm_format_set_silence(sparam.format, silence->data,
			       frames_per_packet*bytes_per_frame);

    // flush network packets, can be plenty
    flushing = 1;
    flushed_packets = 0;
    
    while(flushing) {
	int r;
	struct pollfd fds;

	fds.fd = sock;
	fds.events = POLLIN;

	if ((r = poll(&fds, 1, 100)) == 1) {
	    int len;
	    uint8_t src_buffer[BYTES_PER_PACKET];

	    len = recvfrom(sock, src_buffer, sizeof(src_buffer), 0,
			 (struct sockaddr *) &addr, &addrlen);
	    if (len < 0) {
		perror("recvfrom");
		exit(1);
	    }
	    flushed_packets++;
	}
	else if (r == 0) {
	    flushing = 0;
	    if (verbose) 
		fprintf(stderr, "flushed %ld packets\n", flushed_packets);
	}
    }
    
    while(1) {
	int r;
	struct pollfd fds;

	fds.fd = sock;
	fds.events = POLLIN;

	if ((r = poll(&fds, 1, 1000)) == 1) {
	    acast_t* src;
	    uint8_t src_buffer[BYTES_PER_PACKET];
	    acast_t* dst;
	    uint8_t dst_buffer[BYTES_PER_PACKET*SRC_CHANNELS];

	    src = (acast_t*) src_buffer;
	    r = recvfrom(sock, src_buffer, sizeof(src_buffer), 0, 
			 (struct sockaddr *) &addr, &addrlen);
	    if (r < 0) {
		perror("recvfrom");
		exit(1);
	    }
	    if (r == 0)
		continue;

	    if (debug) {
		if ((r - sizeof(acast_t)) !=
		    src->param.bytes_per_channel *
		    src->param.channels_per_frame*src->num_frames) {
		    fprintf(stderr, "param data mismatch r=%d\n", r);
		    acast_print(stderr, src);
		}
	    }

	    if (memcmp(&src->param, &lparam, sizeof(acast_params_t)) != 0) {
		if (verbose)
		    fprintf(stderr, "new parameters from %s:%d\n",
			    inet_ntoa(addr.sin_addr),
			    ntohs(addr.sin_port));
		
		acast_print(stderr, src);
		snd_pcm_reset(handle);
		snd_pcm_prepare(handle);

		lparam = src->param;
		iparam = lparam;
		iparam.channels_per_frame = num_output_channels;

		acast_setup_param(handle, &iparam, &sparam, &frames_per_packet);

		bytes_per_frame = sparam.bytes_per_channel*
		    sparam.channels_per_frame;
		silence->num_frames = frames_per_packet;
		snd_pcm_format_set_silence(sparam.format,
					   silence->data,
					   frames_per_packet*bytes_per_frame);
		acast_play(handle,bytes_per_frame,
			    silence->data,silence->num_frames);
		acast_play(handle,bytes_per_frame,
			   silence->data,silence->num_frames);
		snd_pcm_start(handle);
	    }

	    switch(map_type) {
	    case 1:
		dst = (acast_t*) dst_buffer;
		dst->param = sparam;
		map_channels(sparam.format,
			     src->data, src->param.channels_per_frame,
			     dst->data, num_output_channels, 
			     channel_map,
			     frames_per_packet);
		break;
	    case 2:
		dst = (acast_t*) dst_buffer;
		dst->param = sparam;
		op_channels(sparam.format,
			    src->data, src->param.channels_per_frame,
			    dst->data, num_output_channels,
			    channel_op, num_channel_ops,
			    frames_per_packet);
		break;
	    case 0:
	    default:
		dst = src;
		dst->param = sparam;
		break;
	    }

	    dst->seqno = src->seqno;
	    dst->num_frames = src->num_frames;

	    if ((verbose > 3) && (dst->seqno % 100 == 0)) {
		acast_print(stderr, dst);
	    }
	    
	    if (!seen_packet) {
		acast_play(handle,bytes_per_frame,
			    silence->data,silence->num_frames);
		acast_play(handle,bytes_per_frame,
			   silence->data,silence->num_frames);
		snd_pcm_start(handle);
	    }

	    if ((len=acast_play(handle, bytes_per_frame, dst->data, dst->num_frames)) < 0) {
		err = len;
		fprintf(stderr, "snd_pcm_writei %s\n", snd_strerror(err));
		snd_pcm_reset(handle);
		snd_pcm_prepare(handle);
		seen_packet = 0;
		continue;
	    }
	    if (len < dst->num_frames) {
		fprintf(stderr, "short write %d\n", len);
	    }

	    if (verbose && seen_packet &&
		((drop=(dst->seqno - last_seqno)) != 1))
		fprintf(stderr, "dropped %u frames\n", drop);
	    if ((verbose > 1) && (dst->seqno % 100) == 0) {
		acast_print(stderr, dst);
	    }
	    seen_packet = 1;
	    last_seqno = dst->seqno;
	}
    }
    exit(0);
}
