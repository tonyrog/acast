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
#include "tick.h"
#include "crc32.h"

#define PLAYBACK_DEVICE "default"
#define NUM_CHANNELS  0
#define CHANNEL_MAP   "auto"
#define SRC_CHANNELS 6           // max number of channels supported

#define MULTICAST_TTL  1
#define MULTICAST_LOOP 0

#define SUB_REFRESH_TIME 10000000  // 10s

#define CLIENT_MODE_UNICAST   1
#define CLIENT_MODE_MULTICAST 2
#define CLIENT_MODE_MIXED     3

void help(void)
{
printf("usage: acast_receiver [options] file\n"
"  -h, --help      print help\n"
"  -v, --verbose   increase verbosity\n"
"  -D, --debug     debug verbosity\n"
"  -M, --multicast multicast mode only\n"
"  -U, --unicast   unicast mode only\n"     
"  -a, --addr      multicast address (%s)\n"
"  -i, --iface     multicast interface address (%s)\n"
"  -p, --port      multicast address port (%d)\n"
"  -q, --ctrl      multicast control port (%d)\n"
"  -l, --loop      enable multi cast loop (%d)\n"
"  -t, --ttl       multicast ttl (%d)\n"       
"  -d, --device    playback device (%s)\n"
"  -c, --channels  number of output channels (%d)\n"
"  -m, --map       channel map (%s)\n",       
       MULTICAST_ADDR,
       INTERFACE_ADDR,
       MULTICAST_PORT,
       CONTROL_PORT,
       MULTICAST_LOOP,
       MULTICAST_TTL,
       PLAYBACK_DEVICE,
       NUM_CHANNELS,
       CHANNEL_MAP);
}

int verbose = 0;
int debug = 0;

void flush_packets(int sock)
{
    size_t flushed_packets = 0;
    int flushing = 1;
    
    while(flushing) {
	int r;
	struct pollfd fds;

	fds.fd = sock;
	fds.events = POLLIN;

	if ((r = poll(&fds, 1, 10)) == 1) {
	    struct sockaddr_in addr;
	    socklen_t addrlen;	
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
}

int send_subscribe(int sock, struct sockaddr_in* addr, socklen_t addrlen,
		   uint32_t id, uint32_t mask)
{
    actl_t sub;

    sub.magic = CONTROL_MAGIC;
    sub.id    = id;
    sub.mask  = mask;             // channel mask
    sub.crc   = 0;
    sub.crc = crc32((uint8_t*)&sub, sizeof(sub));

    if (verbose) {
	fprintf(stderr, "subscribe id=%d, %s:%d channel mask=%08x\n",
		id, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port),
		mask);
    }
    
    return sendto(sock, (void*) &sub, sizeof(sub), 0,
		  (struct sockaddr *) addr, addrlen);
}

int main(int argc, char** argv)
{
    char* playback_device_name = PLAYBACK_DEVICE;
    snd_pcm_t *handle;
    int err;
    int sock;
    int ctrl;
    int len;
    char* multicast_addr = MULTICAST_ADDR;
    char* multicast_ifaddr = INTERFACE_ADDR;    // interface address
    uint16_t multicast_port = MULTICAST_PORT;
    int multicast_loop = MULTICAST_LOOP;        // loopback multicast packets
    int multicast_ttl  = MULTICAST_TTL;
    uint16_t control_port  = CONTROL_PORT;
    struct sockaddr_in addr;
    socklen_t addrlen;
    struct sockaddr_in caddr;
    socklen_t caddrlen;    
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
    acast_channel_ctx_t chan_ctx;    
    size_t bytes_per_frame;
    size_t network_bufsize = BYTES_PER_PACKET;
    int mode = SND_PCM_NONBLOCK;
    uint32_t submask = 0;
    tick_t sub_time = 0;
    int client_mode = CLIENT_MODE_MIXED;
    uint32_t client_id = 0;
    
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
	    {"ctrl",   required_argument, 0,  'q'},
	    {"ttl",    required_argument, 0,  't'},
	    {"loop",   no_argument,       0,  'l'},	    
	    {"device", required_argument, 0,  'd'},
	    {"channels",required_argument, 0, 'c'},
	    {"map",     required_argument, 0, 'm'},
	    {"sub",     required_argument, 0, 's'},
	    {"unicast", no_argument,       0, 'U'},
	    {"multicast", no_argument,     0, 'M'},
	    {"id",      required_argument, 0, 'I'},
	    {0,        0,                  0, 0}
	};
	

	c = getopt_long(argc, argv, "lhvDUMa:i:p:t:d:c:m:s:I:",
                        long_options, &option_index);
	if (c == -1)
	    break;
	switch(c) {
	case 'h':
	    help();
	    exit(0);
	    break;
	case 'U':
	    client_mode = CLIENT_MODE_UNICAST;
	    break;
	case 'M':
	    client_mode = CLIENT_MODE_MULTICAST;
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
	case 'q':
	    control_port = atoi(optarg);
	    if ((control_port < 1) || (control_port > 65535)) {
		fprintf(stderr, "control port out of range\n");
		exit(1);
	    }
	    break;
	case 'c':
	    num_output_channels = atoi(optarg);
	    break;
	case 'm':
	    map = strdup(optarg);
	    break;
	case 's': {  // channel subscription mask
	    char* ptr = optarg;
	    submask = 0;
	    while(*ptr) {
		if (!isdigit(*ptr)) {
		    fprintf(stderr, "sub argument expect digits argument\n");
		    exit(1);
		}
		submask |= (1 << (*ptr - '0'));
		ptr++;
	    }
	    break;
	}
	case 'I':
	    client_id = atoi(optarg);
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

    if (parse_channel_ctx(map,&chan_ctx,SRC_CHANNELS,
			  &num_output_channels) < 0) {
	fprintf(stderr, "map synatx error\n");
	exit(1);
    }

    if (verbose) {
	print_channel_ctx(stdout, &chan_ctx);
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

    if (client_mode == CLIENT_MODE_MULTICAST)
	ctrl = -1;    
    else if ((ctrl = acast_sender_open(multicast_addr,
				       multicast_ifaddr,
				       control_port,
				       multicast_ttl,
				       multicast_loop,
				       &caddr, &caddrlen,
				       network_bufsize)) < 0) {
	fprintf(stderr, "unable to open multicast socket %s\n",
		strerror(errno));
	exit(1);
    }
    else {
	if (verbose) {
	    // Control output
	    fprintf(stderr, "multicast to %s:%d on interface %s\n",
		    multicast_addr, control_port, multicast_ifaddr);
	    fprintf(stderr, "send addr=%s, len=%d\n",
		    inet_ntoa(caddr.sin_addr), caddrlen);
	}
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
			       frames_per_packet*sparam.channels_per_frame);

    // flush_packets(sock);
    
    if ((submask != 0) && (ctrl > -1)) {
	send_subscribe(ctrl, &caddr, caddrlen, client_id, submask);
	sub_time = time_tick_now();
    }
    
    while(1) {
	int r;
	struct pollfd fds[2];
	uint32_t crc;
	
	fds[0].fd = sock;
	fds[0].events = POLLIN;
	fds[1].fd = ctrl;
	fds[1].events = POLLIN;	

	if ((r = poll(fds, 2, 1000)) > 0) {
	    acast_t* src;
	    uint8_t src_buffer[BYTES_PER_PACKET];
	    acast_t* dst;
	    uint8_t dst_buffer[BYTES_PER_PACKET*SRC_CHANNELS];

	    if (fds[0].revents & POLLIN) { // got multicast packet
		src = (acast_t*) src_buffer;
		r = recvfrom(sock, src_buffer, sizeof(src_buffer), 0, 
			     (struct sockaddr *) &addr, &addrlen);
		if (verbose) {
//		    fprintf(stderr, "data packet from multicast %s:%d\n",
//			    inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
		}
		if (r < 0) {
		    perror("recvfrom");
		    exit(1);
		}
		if (r == 0)
		    continue;
	    }
	    else if (fds[1].revents & POLLIN) { // got unicast packet
		src = (acast_t*) src_buffer;
		r = recvfrom(ctrl, src_buffer, sizeof(src_buffer), 0, 
			     (struct sockaddr *) &addr, &addrlen);
		if (verbose) {
//		    fprintf(stderr, "data packet from unicast %s:%d\n",
//			    inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
		}
		if (r < 0) {
		    perror("recvfrom");
		    exit(1);
		}
		if (r == 0)
		    continue;
	    }

	    if (src->magic != ACAST_MAGIC)
		continue;
	    crc = src->crc;
	    src->crc = 0;
	    if (crc32((uint8_t*) src, sizeof(acast_t)) != crc) {
		fprintf(stderr, "crc error packet header corrupt\n");
		continue;
	    }

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
		num_output_channels = iparam.channels_per_frame;

		acast_setup_param(handle, &iparam, &sparam, &frames_per_packet);

		bytes_per_frame = sparam.bytes_per_channel*
		    sparam.channels_per_frame;
		silence->num_frames = frames_per_packet;
		snd_pcm_format_set_silence(sparam.format,silence->data,
	                       frames_per_packet*sparam.channels_per_frame);
		seen_packet = 0;
	    }

	    switch(chan_ctx.type) {
	    case ACAST_MAP_PERMUTE:		
		dst = (acast_t*) dst_buffer;
		dst->param = sparam;
		permute_ii(sparam.format,
			   src->data, src->param.channels_per_frame,
			   dst->data, num_output_channels, 
			   chan_ctx.channel_map,
			   frames_per_packet);
		break;
	    case ACAST_MAP_OP:
		dst = (acast_t*) dst_buffer;
		dst->param = sparam;
		scatter_gather_ii(sparam.format,
				  src->data, src->param.channels_per_frame,
				  dst->data, num_output_channels,
				  chan_ctx.channel_op, chan_ctx.num_channel_ops,
				  frames_per_packet);
		break;
	    case ACAST_MAP_ID:
		dst = src;
		dst->param = sparam;
		break;		
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

	if ((submask != 0) && (ctrl > -1)) {
	    tick_t now = time_tick_now();
	    if ((now - sub_time) >= SUB_REFRESH_TIME) {
		send_subscribe(ctrl, &caddr, caddrlen, client_id, submask);
		sub_time = now;
	    }
	}
    }
    exit(0);
}
