//
//  acast_sender
//
//     open a sound device and read data samples
//     and multicast over ip (ttl = 1)
//
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <getopt.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "acast.h"
#include "tick.h"

#define MAX_CLIENTS     9

#define CAPTURE_DEVICE "default"
#define NUM_CHANNELS  6
#define CHANNEL_MAP   "auto"

// ttl=0 local host, ttl=1 local network
#define MULTICAST_TTL  1
#define MULTICAST_LOOP 0

#define CLIENT_TIMEOUT 30000000  // 30s

#define CLIENT_MODE_UNICAST   1
#define CLIENT_MODE_MULTICAST 2
#define CLIENT_MODE_MIXED     3

typedef struct
{
    struct sockaddr_in  addr;
    socklen_t           addrlen;
    tick_t              tmo;                  // next timeout
    uint32_t            mask;                 // channel mask
    int                 num_output_channels;
    acast_channel_ctx_t chan_ctx;
    uint8_t             buffer[BYTES_PER_PACKET];
} client_t;

// client 0 is the default multicast client
static int num_clients = 1;
client_t client[MAX_CLIENTS];

#define min(a,b) (((a)<(b)) ? (a) : (b))
#define max(a,b) (((a)>(b)) ? (a) : (b))

int verbose = 0;
int debug = 0;


void help(void)
{
printf("usage: acast_sender [options]\n"
"  -h, --help      print help\n"
"  -v, --verbose   increase verbosity\n"
"  -D, --debug     debug verbosity\n"
"  -M, --multicast multicast mode only\n"
"  -U, --unicast   unicast mode only\n"
"  -a, --maddr     multicast address (%s)\n"
"  -i, --iface     multicast interface address (%s)\n"
"  -u, --uaddr     unicast client address[:port] (multiple)\n"
"  -p, --port      multicast address port (%d)\n"
"  -q, --ctrl      multicast control port (%d)\n"
"  -l, --loop      enable multi cast loop (%d)\n"
"  -t, --ttl       multicast ttl (%d)\n"
"  -d, --device    capture device (%s)\n"
"  -c, --channels  number of output channels (%d)\n"
"  -C, --ichannels  number of input channels (%d)\n"
"  -m, --map       channel map (%s)\n",
       MULTICAST_ADDR,
       INTERFACE_ADDR,
       MULTICAST_PORT,
       CONTROL_PORT,
       MULTICAST_LOOP,       
       MULTICAST_TTL,
       CAPTURE_DEVICE,
       NUM_CHANNELS,
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

int client_add(struct sockaddr_in* addr, socklen_t addrlen, uint32_t mask)
{
    int i;
    for (i = 0; i < num_clients; i++) {
	// maybe compare components?
	if (memcmp(&client[i].addr, &addr, addrlen) == 0) {
	    client[i].tmo = time_tick_now() + CLIENT_TIMEOUT;
	    if (mask != client[i].mask) {
		set_client_mask(&client[i], mask);
		if (verbose) {
		    fprintf(stderr, "unicast client [%d] %s:%d updated\n",
			    i, inet_ntoa(addr->sin_addr),
			    ntohs(addr->sin_port));
		}
	    }
	    return 0;
	}
    }
    if (num_clients < MAX_CLIENTS) {
	i = num_clients++;
	client[i].addr = *addr;
	client[i].addrlen = addrlen;
	client[i].tmo = time_tick_now() + CLIENT_TIMEOUT;	
	set_client_mask(&client[i], mask);

	if (verbose) {
	    fprintf(stderr, "unicast client[%d] %s:%d added\n",
		    i, inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
	}
	return 0;
    }
    return -1;
}

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
	if (client_add(&uaddr, uaddrlen, (1<<i)) < 0)
	    return -1;
    }
    return 0;
}


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
    int sock, ctrl;
    char* multicast_addr = MULTICAST_ADDR;
    uint16_t multicast_port = MULTICAST_PORT;
    int multicast_loop = MULTICAST_LOOP;       // loopback multicast packets
    int multicast_ttl  = MULTICAST_TTL;
    char* interface_addr = INTERFACE_ADDR; // interface address
    uint16_t control_port = CONTROL_PORT;
    int num_input_channels = 2;
    char* map = CHANNEL_MAP;
    struct sockaddr_in maddr;
    socklen_t maddrlen;
    struct sockaddr_in iaddr;
    socklen_t iaddrlen;
    struct sockaddr_in addr;
    socklen_t addrlen;        
    size_t bytes_to_send;    
    size_t network_bufsize = 2*BYTES_PER_PACKET;
    tick_t     last_time;
    tick_t     report_time;    
    uint64_t   sent_frames = 0;
    uint64_t   sent_bytes  = 0;    
    int pcm_mode = 0; // SND_PCM_NONBLOCK;
    char src_buffer[BYTES_PER_PACKET];
    acast_t* src;
    size_t num_uclients = 0;
    char* uclient[MAX_CLIENTS];
    int client_mode = CLIENT_MODE_MIXED;

    while(1) {
	int option_index = 0;
	int c;
	static struct option long_options[] = {
	    {"help",   no_argument,         0, 'h'},
	    {"verbose",no_argument,         0, 'v'},
	    {"maddr",  required_argument,   0, 'a'},
	    {"iface",  required_argument,   0, 'i'},
	    {"uaddr",  required_argument,   0, 'u'},
	    {"port",   required_argument,   0, 'p'},
	    {"ctrl",   required_argument,   0, 'q'},
	    {"ttl",    required_argument,   0, 't'},
	    {"loop",   no_argument,         0, 'l'},
	    {"device", required_argument,   0, 'd'},
	    {"channels",required_argument,  0, 'c'},
	    {"ichannels",required_argument, 0, 'C'},	    
	    {"map",     required_argument,  0, 'm'},
	    {"unicast", no_argument,        0, 'U'},
	    {"multicast", no_argument,      0, 'M'},
	    {0,        0,                   0, 0}
	};
	
	c = getopt_long(argc, argv, "lhvDUMa:u:i:p:q:t:d:c:C:m:",
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
	    capture_device_name = strdup(optarg);
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

    parse_clients(uclient, num_uclients, multicast_port);

    time_tick_init();    

    if ((err = snd_pcm_open(&handle, capture_device_name,
			    SND_PCM_STREAM_CAPTURE, pcm_mode)) < 0) {
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

    if (parse_channel_ctx(map,&client[0].chan_ctx,sparam.channels_per_frame,
			  &client[0].num_output_channels) < 0) {
	fprintf(stderr, "map synatx error\n");
	exit(1);
    }

    if (verbose) {
	print_channel_ctx(stdout, &client[0].chan_ctx);
	printf("num_output_channels = %d\n", client[0].num_output_channels);
    }

    mparam = sparam;
    mparam.channels_per_frame = client[0].num_output_channels;
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

    if ((sock = acast_sender_open(multicast_addr,
				  interface_addr,
				  multicast_port,
				  multicast_ttl,
				  multicast_loop,
				  &maddr, &maddrlen,
				  network_bufsize)) < 0) {
	fprintf(stderr, "unable to open multicast socket %s\n",
		strerror(errno));
	exit(1);
    }
    client[0].addr = maddr;
    client[0].addrlen = maddrlen;
    client[0].tmo = 0;

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
	    fprintf(stderr, "multicast from %s:%d on interface %s\n",
		    multicast_addr, control_port, interface_addr);
	    fprintf(stderr, "recv addr=%s, len=%d\n",
		    inet_ntoa(iaddr.sin_addr), iaddrlen);
	}
    }

    if (verbose) {
	if (client_mode != CLIENT_MODE_UNICAST) {
	    // Mulicast data output
	    fprintf(stderr, "multicast to %s:%d\n",
		    multicast_addr, multicast_port);
	    fprintf(stderr, "send from interface %s ttl=%d loop=%d\n",
		    interface_addr, multicast_ttl,  multicast_loop);
	    fprintf(stderr, "send to addr=%s, len=%d\n",
		    inet_ntoa(maddr.sin_addr), maddrlen);
	}
    }

    frames_per_packet = min(mcast_frames_per_packet,snd_frames_per_packet);
    bytes_per_frame = client[0].num_output_channels * mparam.bytes_per_channel;

    last_time = time_tick_now();
    report_time = last_time;
    
    while(1) {
	int r;

	if (ctrl >= 0) {  // client_mode != CLIENT_MODE_UNICAST
	    struct pollfd fds;
	    fds.fd = ctrl;
	    fds.events = POLLIN;

	    if ((r = poll(&fds, 1, 0)) == 1) { // control input
		actl_t* ctl;
		uint8_t  ctl_buffer[BYTES_PER_PACKET];

		ctl = (actl_t*) ctl_buffer;
		r = recvfrom(ctrl, (void*) ctl, sizeof(ctl_buffer), 0,
			     (struct sockaddr *) &addr, &addrlen);
		if (ctl->magic == CONTROL_MAGIC) {
		    client_add(&addr, addrlen, ctl->mask);
		}
	    }
	}
	
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
	    int cstart=0, cnum=0;
	    int i=0;
	    
	    if (r < frames_per_packet)
		fprintf(stderr, "acast_read read short bytes\n");

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
	    for (i=cstart; i<cnum; i++) {
		switch(client[i].chan_ctx.type) {
		case ACAST_MAP_PERMUTE:
		    dst = (acast_t*) dst_buffer;
		    dst->param = mparam;
		    permute_ii(mparam.format,
			       src->data, sparam.channels_per_frame,
			       dst->data, client[i].num_output_channels,
			       client[i].chan_ctx.channel_map,
			       frames_per_packet);
		    break;
		case ACAST_MAP_OP:
		    dst = (acast_t*) dst_buffer;
		    dst->param = mparam;
		    scatter_gather_ii(mparam.format,
				      src->data, sparam.channels_per_frame,
				      dst->data, client[i].num_output_channels,
				      client[i].chan_ctx.channel_op,
				      client[i].chan_ctx.num_channel_ops,
				      frames_per_packet);
		    break;
		case ACAST_MAP_ID:
		    dst = src;
		    dst->param = mparam;
		    break;
		default:
		    dst = src;
		    // error
		    break;
		}
		dst->seqno = seqno++;
		dst->num_frames = r;
		dst->param.channels_per_frame = client[i].num_output_channels;
		bytes_per_frame = client[i].num_output_channels *
		    mparam.bytes_per_channel;
		bytes_to_send = bytes_per_frame*dst->num_frames;

		if (sendto(sock, (void*)dst, sizeof(acast_t)+bytes_to_send, 0,
			   (struct sockaddr *) &client[i].addr,
			   client[i].addrlen) < 0) {
		    fprintf(stderr, "failed to send frame %s\n",
			    strerror(errno));
		}
		sent_frames += dst->num_frames;
		sent_bytes  += bytes_to_send;
	    }
	    if (sent_frames >= 100000) {
		if (verbose > 1) {
		    tick_t now = time_tick_now();
		    fprintf(stderr, "SEND RATE = %.2fKHz, %.2fMb/s\n",
			    (1000*sent_frames)/
			    ((double)(now - report_time)),
			    ((1000000*sent_bytes)/
			     (double)(now - report_time)) /
			    (double)(1024*1024));
		    report_time = now;
		}
		if (verbose > 3)
		    acast_print(stderr, src);
		sent_frames = 0;
		sent_bytes = 0;
	    }
	}
    }
    exit(0);
}
