//
//  acast_player
//
//     play audio files
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <endian.h>
#include <ctype.h>
#include <getopt.h>
#include <sys/types.h>

#include "acast.h"
#include "acast_file.h"
#include "tick.h"

#define PLAYBACK_DEVICE "default"
#define NUM_CHANNELS   2
#define CHANNEL_MAP   "auto"

#define MAX_U_32_NUM    0xFFFFFFFF

int verbose = 0;
int debug = 0;

#define min(a,b) (((a)<(b)) ? (a) : (b))
#define max(a,b) (((a)>(b)) ? (a) : (b))

#define BYTES_PER_BUFFER ((1152*2*2)+sizeof(acast_t))


void help(void)
{
printf("usage: acast_player [options] file\n"
"  -h, --help      print help\n"
"  -v, --verbose   increase verbosity\n"
"  -D, --debug     debug verbosity\n"
"  -d, --device    playback device (\"%s\")\n"
"  -c, --channels  number of output channels (%d)\n"
"  -m, --map       channel map (\"%s\")\n",       
       PLAYBACK_DEVICE,
       NUM_CHANNELS,
       CHANNEL_MAP);       
}

int main(int argc, char** argv)
{
    char* playback_device_name = PLAYBACK_DEVICE;
    char* filename;
    snd_pcm_t *handle;
    acast_params_t iparam;
    acast_params_t sparam;
    snd_pcm_uframes_t af_frames_per_packet;
    snd_pcm_uframes_t snd_frames_per_packet;
    snd_pcm_uframes_t frames_per_packet;
    size_t snd_bytes_per_frame;
    size_t bytes_per_frame;
    uint32_t seqno = 0;
    int play_started = 0;
    int err;
    int n;
    uint32_t num_frames;
    int num_output_channels = 0;
    char* map = CHANNEL_MAP;
    acast_channel_ctx_t chan_ctx;
    char       silence_buffer[BYTES_PER_PACKET];
    acast_t*   silence;
    acast_file_t* af;
    int mode = 0; // SND_PCM_NONBLOCK;
    
    while(1) {
	int option_index = 0;
	int c;
	static struct option long_options[] = {
	    {"help",   no_argument, 0,       'h'},
	    {"verbose",no_argument, 0,       'v'},
	    {"debug",  no_argument, 0,       'D'},
	    {"device", required_argument, 0, 'd'},
	    {"channels",required_argument, 0, 'c'},
	    {"map",     required_argument, 0, 'm'},	    
	    {0,        0,                 0, 0}
	};
	
	c = getopt_long(argc, argv, "hvDd:c:m:",
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

    if ((err=snd_pcm_open(&handle,playback_device_name,
			  SND_PCM_STREAM_PLAYBACK,mode)) < 0) {
	fprintf(stderr, "snd_pcm_open failed %s\n", snd_strerror(err));
	exit(1);
    }
    acast_clear_param(&iparam);
    // setup wanted paramters
    iparam.format = af->param.format;
    iparam.sample_rate = af->param.sample_rate;
    iparam.channels_per_frame = num_output_channels;
    acast_setup_param(handle, &iparam, &sparam, &snd_frames_per_packet);
    snd_bytes_per_frame =
	sparam.bytes_per_channel*sparam.channels_per_frame;
    
    if (verbose) {
	fprintf(stderr, "snd params:\n");
	acast_print_params(stderr, &sparam);
	fprintf(stderr, "  snd_bytes_per_frame = %lu\n",
		snd_bytes_per_frame);
	fprintf(stderr, "  snd_frames_per_packet = %lu\n",
		snd_frames_per_packet);
    }
    
    silence =  (acast_t*) silence_buffer;
    silence->num_frames = snd_frames_per_packet;
    snd_pcm_format_set_silence(sparam.format, silence->data,
			       snd_frames_per_packet*
			       sparam.channels_per_frame);
    

    frames_per_packet = min(snd_frames_per_packet,af_frames_per_packet);

    bytes_per_frame = num_output_channels * sparam.bytes_per_channel;

    if (verbose) {
	fprintf(stderr, "  num_output_channels = %d\n", num_output_channels);
	fprintf(stderr, "  bytes_per_frame = %lu\n",  bytes_per_frame);
	fprintf(stderr, "  frames_per_packet = %lu\n", frames_per_packet);
    }

    // fixme we probably need cast_buffer and play_buffer!!!!
    num_frames = af->num_frames;
    while((num_frames==MAX_U_32_NUM) || (num_frames >= frames_per_packet)) {
	acast_buffer_t abuf;
	char src_buffer[BYTES_PER_BUFFER];
	acast_t* src;
	char dst_buffer[BYTES_PER_BUFFER];
	acast_t* dst;
	
	src = (acast_t*) src_buffer;
	if ((n=acast_file_read(af, &abuf, src->data,
			       BYTES_PER_BUFFER-sizeof(acast_t),
			       frames_per_packet)) < 0) {
	    fprintf(stderr, "read error: %s\n", strerror(errno));
	    exit(1);
	}
	
	switch(chan_ctx.type) {
	case ACAST_MAP_ID:	     // must always copy!
	case ACAST_MAP_PERMUTE:
	    dst = (acast_t*) dst_buffer;
	    dst->param = sparam;
	    permute_ni(sparam.format,
		       abuf.data, abuf.stride, abuf.size,
		       dst->data, num_output_channels, 
		       chan_ctx.channel_map,
		       n);
	    break;
	case ACAST_MAP_OP:
	    dst = (acast_t*) dst_buffer;
	    dst->param = sparam;
	    scatter_gather_ni(sparam.format,
			      abuf.data, abuf.stride, abuf.size,
			      dst->data, num_output_channels,
			      chan_ctx.channel_op, chan_ctx.num_channel_ops,
			      n);
	    break;
	default:
	    fprintf(stderr, "bad ctx type %d\n", chan_ctx.type);
	    exit(1);
	}
	if (num_frames < MAX_U_32_NUM)
	    num_frames -= n;
	dst->seqno = seqno++;
	dst->num_frames = n;
	if ((verbose > 3) && (dst->seqno % 100 == 0)) {
	    acast_print(stderr, dst);
	}
	if (!play_started) {
	  acast_play(handle, snd_bytes_per_frame,
		     silence->data, silence->num_frames);
	  acast_play(handle, snd_bytes_per_frame,
		     silence->data, silence->num_frames);
	  snd_pcm_start(handle);
	  play_started = 1;
	}
	acast_play(handle, bytes_per_frame, dst->data, dst->num_frames);
    }
    exit(0);
}
