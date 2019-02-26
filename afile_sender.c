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
#include "crc32.h"

#define MAX_CLIENTS     9
// ttl=0 local host, ttl=1 local network
#define MULTICAST_TTL  1
#define MULTICAST_LOOP 0
#define NUM_CHANNELS   0
#define CHANNEL_MAP   "auto"

// client 0 is the default multicast client
static int num_clients = 1;
client_t client[MAX_CLIENTS];

int verbose = 0;
int debug = 0;

void help(void)
{
printf("usage: afile_sender [options] file\n"
"  -h, --help      print help\n"
"  -v, --verbose   increase verbosity\n"
"  -D, --debug     debug verbosity\n"
"  -M, --multicast multicast mode only\n"
"  -U, --unicast   unicast mode only\n"
"  -a, --maddr     multicast address (\"%s\")\n"
"  -i, --iface     multicast interface address (\"%s\")\n"
"  -u, --uaddr     unicast client address[:port] (multiple)\n"       
"  -p, --port      multicast address port (%d)\n"
"  -q, --ctrl      multicast control port (%d)\n"       
"  -l, --loop      enable multi cast loop (%d)\n"
"  -t, --ttl       multicast ttl (%d)\n"
"  -c, --channels  number of output channels (%d)\n"
"  -m, --map       channel map (\"%s\")\n",       
       MULTICAST_ADDR,
       INTERFACE_ADDR,
       MULTICAST_PORT,
       CONTROL_PORT,       
       MULTICAST_LOOP,       
       MULTICAST_TTL,
       NUM_CHANNELS,
       CHANNEL_MAP);       
}

void set_client_mask(client_t* cp, uint32_t mask)
{
    int j = 0;
    int m = 0;

    cp->mask = mask; // save mask for easy check etc
    while(mask && (j < MAX_CHANNEL_MAP)) {
	if (mask & 1) {
	    acast_op_t op = MAP_SRC(j,m);
	    cp->chan_ctx.channel_map[j] = m;
	    cp->chan_ctx.channel_op[j] = op; // MAP_SRC(j,m);
	    j++;
	}
	mask >>= 1;
	m++;
    }
    cp->chan_ctx.type = ACAST_MAP_PERMUTE;
    cp->chan_ctx.num_channel_ops = j;
    cp->num_output_channels = j;
}

// fixme store compare struct sockaddr ?
int client_add(uint32_t id,struct sockaddr_in* addr,socklen_t addrlen,
	       uint32_t mask)
{
    int i;
    for (i = 0; i < num_clients; i++) {
	// maybe compare components?
	if ((client[i].id && (client[i].id == id)) ||
	    ((client[i].id == 0) && (id == 0) &&
	     (memcmp(&client[i].addr, &addr, addrlen) == 0))) {
	    int updated = 0;
	    client[i].tmo = time_tick_now() + CLIENT_TIMEOUT;
	    if (mask != client[i].mask) {
		set_client_mask(&client[i], mask);
		updated = 1;
	    }
	    if (memcmp(&client[i].addr, &addr, addrlen) != 0) {
		client[i].addr = *addr;
		updated = 1;
	    }
	    if (updated && verbose) {
		fprintf(stderr, "unicast client [%d] id=%d %s:%d updated\n",
			i, client[i].id,
			inet_ntoa(addr->sin_addr),
			ntohs(addr->sin_port));
	    }
	    return 0;
	}
    }
    if (num_clients < MAX_CLIENTS) {
	i = num_clients++;
	client[i].id   = id;
	client[i].addr = *addr;
	client[i].addrlen = addrlen;
	client[i].tmo = time_tick_now() + CLIENT_TIMEOUT;
	client[i].ptr = client[i].buffer;	
	set_client_mask(&client[i], mask);

	if (verbose) {
	    fprintf(stderr, "unicast client[%d] id=%d %s:%d added\n",
		    i, id, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
	}
	return 0;
    }
    return -1;
}

// parse client list -u <ip>:<port> -u <ip>:<port> ...
int parse_clients(char** uclient, size_t num_uclients, uint16_t default_port)
{
    int i;
    
    for (i = 0; i < num_uclients; i++) {
	char* p;
	uint16_t uport;
	struct sockaddr_in uaddr;
	socklen_t          uaddrlen;	    
	    
	if ((p = strrchr(uclient[i], ':')) != NULL) {
	    *p = '\0';
	    uport = atoi(p+1);
	}
	else
	    uport = default_port;

	uaddrlen = sizeof(uaddr);
	memset((char *)&uaddr, 0, uaddrlen);
	if (!inet_aton(uclient[i], &uaddr.sin_addr)) {
	    fprintf(stderr, "address syntax error [%s]\n", uclient[i]);
	    return -1;
	}
	uaddr.sin_family = AF_INET;
	uaddr.sin_port = htons(uport);
	// fixme, make channels flexibel
	if (client_add(0, &uaddr, uaddrlen, (1<<i)) < 0)
	    return -1;
    }
    return 0;
}

int main(int argc, char** argv)
{
    char* filename;
    acast_params_t mparam;
    snd_pcm_uframes_t af_frames_per_packet;    
    snd_pcm_uframes_t frames_per_packet;    
    uint32_t seqno = 0;
    int sock, ctrl;
    char* multicast_addr = MULTICAST_ADDR;
    uint16_t multicast_port = MULTICAST_PORT;
    int multicast_loop = MULTICAST_LOOP;   // loopback multicast packets
    int multicast_ttl  = MULTICAST_TTL;
    char* interface_addr = INTERFACE_ADDR;    // interface address
    uint16_t control_port = CONTROL_PORT;    
    struct sockaddr_in addr;
    socklen_t addrlen;    
    struct sockaddr_in iaddr;
    socklen_t iaddrlen;    
    uint8_t src_buffer[BYTES_PER_BUFFER];
    int frames_remain;
    int num_frames;
    char* map = CHANNEL_MAP;
    size_t network_bufsize = 4*BYTES_PER_PACKET;
    tick_t report_time;
    int first_frame = 1;
    tick_t send_time = 0;
    uint64_t frame_delay_us;   // delay per frame in us
    uint64_t sent_frames = 0;  // number of frames sent
    uint64_t sent_bytes = 0;   // number of bytes sent
    acast_file_t* af;
    acast_buffer_t abuf;
    size_t num_uclients = 0;
    char* uclient[MAX_CLIENTS];
    int client_mode = CLIENT_MODE_MIXED;

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
	    {"unicast", no_argument,       0, 'U'},
	    {"multicast", no_argument,     0, 'M'},	    
	    {0,        0,                 0, 0}
	};
	
	c = getopt_long(argc, argv, "lhvDUMa:u:i:p:t:c:m:",
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
	case 'a':
	    multicast_addr = strdup(optarg);
	    break;
	case 'i':
	    interface_addr = strdup(optarg);
	    break;
	case 'u':
	    if (num_uclients >= MAX_CLIENTS-1) {
		fprintf(stderr, "too many clients max %d\n",
			MAX_CLIENTS-1);
	    }
	    uclient[num_uclients++] = strdup(optarg);
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
	    client[0].num_output_channels = atoi(optarg);	    
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

    parse_clients(uclient, num_uclients, multicast_port);    

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

    if (parse_channel_ctx(map,&client[0].chan_ctx,
			  af->param.channels_per_frame,
			  &client[0].num_output_channels) < 0) {
	fprintf(stderr, "map synatx error\n");
	exit(1);
    }
    
    if (verbose) {
	print_channel_ctx(stdout, &client[0].chan_ctx);
	printf("num_output_channels = %d\n", client[0].num_output_channels);
    }    

    if (af->param.format == SND_PCM_FORMAT_UNKNOWN) {
	fprintf(stderr, "unsupport audio format\n");
	exit(1);
    }
    
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
    
    client[0].addr = addr;
    client[0].addrlen = addrlen;
    client[0].tmo = 0;
    client[0].ptr = client[0].buffer;
    
    if (client_mode == CLIENT_MODE_MULTICAST)
	ctrl = -1;
    else if ((ctrl = acast_receiver_open(multicast_addr,
					 interface_addr,
					 control_port,
					 &iaddr, &iaddrlen,
					 client[0].num_output_channels*
					 network_bufsize)) < 0) {
	fprintf(stderr, "unable to open multicast socket %s\n",
		strerror(errno));
	exit(1);
    }
    else {
	if (verbose) {
	    // Control input
	    fprintf(stderr, "control from %s:%d on interface %s\n",
		    multicast_addr, control_port, interface_addr);
	    fprintf(stderr, "recv addr=%s, len=%d\n",
		    inet_ntoa(iaddr.sin_addr), iaddrlen);
	}	
    }

    if (verbose) {
	if (client_mode != CLIENT_MODE_UNICAST) {
	    // Mulicast data output
	    fprintf(stderr, "data to %s:%d\n",
		    multicast_addr, multicast_port);
	    fprintf(stderr, "sent from interface %s ttl=%d loop=%d\n",
		    interface_addr, multicast_ttl,  multicast_loop);
	    fprintf(stderr, "send to addr=%s, len=%d\n",
		    inet_ntoa(addr.sin_addr), addrlen);
	}
    }
    
    mparam = af->param;
    mparam.channels_per_frame = client[0].num_output_channels;
    frames_per_packet = acast_get_frames_per_packet(&mparam);
    frame_delay_us = (frames_per_packet*1000000) / mparam.sample_rate;
    
    if (verbose > 1) {
	acast_print_params(stderr, &mparam);
	fprintf(stderr, "frames_per_packet=%ld\n", frames_per_packet);
    }
	
    frames_remain = 0;  // samples that remain from last round
    
    report_time = time_tick_now();
    
    while((num_frames = acast_file_read(af, &abuf, src_buffer,
					BYTES_PER_BUFFER-sizeof(acast_t),
					frames_per_packet)) > 0) {
	int cstart=0, cnum=0;
	int i=0;
	
	if (ctrl >= 0) {  // client_mode != CLIENT_MODE_UNICAST
	    int r;
	    uint32_t crc;
	    struct pollfd fds;
	    fds.fd = ctrl;
	    fds.events = POLLIN;

	    if ((r = poll(&fds, 1, 0)) == 1) { // control input
		actl_t* ctl;
		uint8_t  ctl_buffer[BYTES_PER_PACKET];

		if (verbose) {
		    fprintf(stderr, "poll ctrl channel\n");
		}		

		ctl = (actl_t*) ctl_buffer;
		r = recvfrom(ctrl, (void*) ctl, sizeof(ctl_buffer), 0,
			     (struct sockaddr *) &addr, &addrlen);
		if (verbose) {
		    fprintf(stderr, "control packet from %s:%d\n",
			    inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
		}

		crc = ctl->crc;
		ctl->crc = 0;
		if ((ctl->magic == CONTROL_MAGIC) &&
		    crc32((uint8_t*) ctl, sizeof(actl_t)) == crc) {
		    client_add(ctl->id, &addr, addrlen, ctl->mask);
		}
	    }
	}

	switch(client_mode) {
	case CLIENT_MODE_UNICAST:
	    cstart = 1; cnum = num_clients;
	    break;
	case CLIENT_MODE_MULTICAST:
	    cstart = 0; cnum = 1;
	    break;
	case CLIENT_MODE_MIXED:
	    cstart = 0; cnum = num_clients;
	    break;
	default:
	    break;
	}

	for (i = cstart; i < cnum; i++) {
	    // convert all frames for all clients
	    switch(client[i].chan_ctx.type) {
	    case ACAST_MAP_ID:
	    case ACAST_MAP_PERMUTE:
		permute_ni(mparam.format,
			   abuf.data, abuf.stride, abuf.size,
			   client[i].ptr, client[i].num_output_channels,
			   client[i].chan_ctx.channel_map,
			   num_frames);
		break;
	    case ACAST_MAP_OP:
		scatter_gather_ni(mparam.format,
				  abuf.data, abuf.stride, abuf.size,
				  client[i].ptr, client[i].num_output_channels,
				  client[i].chan_ctx.channel_op,
				  client[i].chan_ctx.num_channel_ops,
				  num_frames);
		break;
	    default:
		fprintf(stderr, "bad ctx type %d\n", client[i].chan_ctx.type);
		exit(1);	
	    }
	    client[i].ptr = client[i].buffer;
	}
	
	num_frames += frames_remain;

	while(num_frames >= frames_per_packet) {
	    uint8_t packet_buffer[BYTES_PER_PACKET];
	    acast_t* packet;
	    size_t  bytes_to_send;

	    packet = (acast_t*) packet_buffer;
	    packet->magic = ACAST_MAGIC;
	    packet->param = mparam;
	    packet->seqno = seqno++;
	    packet->num_frames = frames_per_packet;

	    for (i = cstart; i < cnum; i++) {
		int num_channels = client[i].num_output_channels;
		size_t bytes_per_frame = num_channels*mparam.bytes_per_channel;
		packet->param.channels_per_frame = num_channels;
		bytes_to_send = frames_per_packet*bytes_per_frame;
		
		memcpy(packet->data, client[i].ptr, bytes_to_send);
		client[i].ptr += bytes_to_send;
	    
		if ((verbose > 3) && (seqno % 100 == 0)) {
		    acast_print(stderr, packet);
		}
		packet->crc = 0;
		packet->crc = crc32((uint8_t*)packet,sizeof(acast_t));

		// fixme send using sendmsg! keep packet header separate
		if (sendto(sock,(void*)packet,sizeof(acast_t)+bytes_to_send, 0,
			   (struct sockaddr *) &client[i].addr,
			   client[i].addrlen) < 0) {
		    fprintf(stderr, "failed to send frame %s\n",
			    strerror(errno));
		}
		if (first_frame) {
		    send_time = time_tick_now();
		    first_frame = 0;
		}
		sent_frames += frames_per_packet;
		sent_bytes += bytes_to_send;
	    }

	    if (sent_frames >= 100000) {
		if (verbose > 1) {
		    tick_t now = time_tick_now();
		    double td = (now - report_time);
		    fprintf(stderr, "SEND RATE = %.2fKHz, %.2fMb/s\n",
			    (1000*sent_frames)/td,
			    ((1000000*sent_bytes)/td)/(double)(1024*1024));
		    report_time = now;
		}
		sent_frames = 0;
		sent_bytes = 0;
	    }
	    time_tick_wait_until(send_time + frame_delay_us);
	    // send_time is the absolute send time mark
	    send_time += frame_delay_us;
	    num_frames -= frames_per_packet;
	}

	for (i = cstart; i < cnum; i++) {
	    int num_channels = client[i].num_output_channels;
	    size_t bytes_per_frame = num_channels*mparam.bytes_per_channel;
	    memcpy(client[i].buffer, client[i].ptr, num_frames*bytes_per_frame);
	    client[i].ptr = client[i].buffer + num_frames*bytes_per_frame;
	}
	frames_remain = num_frames;
    }
    exit(0);
}
