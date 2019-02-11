//
//  mp3_player
//
//     open a mp3 file and play data samples
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

#define PLAYBACK_DEVICE "default"
#define NUM_CHANNELS   2

#define CHANNEL_MAP   "auto"
#define MAX_CHANNEL_OP  16
#define MAX_CHANNEL_MAP 8

#define MAX_U_32_NUM    0xFFFFFFFF
#define MP3BUFFER_SIZE  4096
#define MP3TAG_SIZE     4096
#define PCM_BUFFER_SIZE 1152

int verbose = 0;
int debug = 0;

#define min(a,b) (((a)<(b)) ? (a) : (b))
#define max(a,b) (((a)>(b)) ? (a) : (b))

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
printf("usage: mp3_player [options] file\n"
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

static inline uint32_t mp3_get_bytes_per_frame(mp3data_struct* mp3)
{
    return mp3->stereo*2;  // 2 byte per channel 
}

static inline snd_pcm_uframes_t mp3_get_frames_per_buffer(mp3data_struct* mp3,
							  size_t buffer_size)
{
    return buffer_size / mp3_get_bytes_per_frame(mp3);
}


int main(int argc, char** argv)
{
    char* playback_device_name = PLAYBACK_DEVICE;
    char* filename;
    snd_pcm_t *handle;
    acast_params_t iparam;
    acast_params_t sparam;
    snd_pcm_uframes_t mp3_frames_per_packet;
    snd_pcm_uframes_t snd_frames_per_packet;    
    snd_pcm_uframes_t frames_per_packet;
    size_t snd_bytes_per_frame;    
    size_t bytes_per_frame;    
    uint32_t seqno = 0;
    int play_started = 0;
    int err;
    hip_t hip;
    snd_pcm_format_t fmt;    
    mp3data_struct mp3;
    int16_t pcm_l[2*PCM_BUFFER_SIZE];
    int16_t pcm_r[2*PCM_BUFFER_SIZE];
    int pcm_remain;
    int fd;
    int num_frames;
    int enc_delay, enc_padding;
    int num_output_channels = 0;    
    char* map = CHANNEL_MAP;
    acast_channel_ctx_t chan_ctx;    
    char       silence_buffer[BYTES_PER_PACKET];
    acast_t*   silence;    
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
    
    filename = argv[optind];
    if ((fd = open(filename, O_RDONLY)) < 0) {
	fprintf(stderr, "error: unable to open %s: %s\n",
		filename, strerror(errno));
	exit(1);
    }

    time_tick_init();
    
    seqno = 0;
    
    if ((hip = hip_decode_init()) == NULL) {
	perror("hip_decode_init");
	exit(1);
    }

    hip_set_msgf(hip, verbose ? emit_message : 0);
    hip_set_errorf(hip, verbose ? emit_error : 0);
    hip_set_debugf(hip, emit_debug);

    time_tick_init();    

    if (mp3_decode_init(fd, hip, &mp3, &enc_delay, &enc_padding) < 0) {
	fprintf(stderr, "failed detect mp3 file format\n");
	exit(1);
    }

    mp3_frames_per_packet =
	mp3_get_frames_per_buffer(&mp3,BYTES_PER_PACKET - sizeof(acast_t));    

    // normally we want to acast 6 channels but now we select the
    // native mp3 format for testing

    if (parse_channel_ctx(map,&chan_ctx,mp3.stereo,
			  &num_output_channels) < 0) {
	fprintf(stderr, "map synatx error\n");
	exit(1);
    }

    if (verbose) {
	print_channel_ctx(stdout, &chan_ctx);
	printf("num_output_channels = %d\n", num_output_channels);
    }
    
    if (verbose > 1) {
	mp3_print(stderr, &mp3);
	fprintf(stderr, "enc_delay=%d\n", enc_delay);
	fprintf(stderr, "enc_padding=%d\n", enc_padding);
    }

    fmt = SND_PCM_FORMAT_S16_LE;

    if ((err=snd_pcm_open(&handle,playback_device_name,
			  SND_PCM_STREAM_PLAYBACK,mode)) < 0) {
	fprintf(stderr, "snd_pcm_open failed %s\n", snd_strerror(err));
	exit(1);
    }

    acast_clear_param(&iparam);
    // setup wanted paramters
    iparam.format = fmt;
    iparam.sample_rate = mp3.samplerate;
    iparam.channels_per_frame = num_output_channels;
    acast_setup_param(handle, &iparam, &sparam, &snd_frames_per_packet);
    snd_bytes_per_frame =
	sparam.bytes_per_channel*sparam.channels_per_frame;

    if (verbose) {
	fprintf(stderr, "snd params:\n");
	acast_print_params(stderr, &sparam);
	fprintf(stderr, "  snd_bytes_per_frame:%ld\n",
		snd_bytes_per_frame);
	fprintf(stderr, "  snd_frames_per_packet:%ld\n",
		snd_frames_per_packet);
    }    
    
    silence =  (acast_t*) silence_buffer;
    silence->num_frames = snd_frames_per_packet;
    snd_pcm_format_set_silence(sparam.format, silence->data,
			       snd_frames_per_packet*
			       sparam.channels_per_frame);
    
    frames_per_packet = min(snd_frames_per_packet,mp3_frames_per_packet);

    bytes_per_frame = num_output_channels * sparam.bytes_per_channel;

    if (verbose) {
	fprintf(stderr, "  num_output_channels:%d\n", num_output_channels);
	fprintf(stderr, "  bytes_per_frame:%ld\n",  bytes_per_frame);
	fprintf(stderr, "  frames_per_packet:%ld\n", frames_per_packet);
    }
    
    pcm_remain = 0;  // samples that remain from last round
    
    while((num_frames = mp3_decode(fd, hip,
				   &pcm_l[pcm_remain],
				   &pcm_r[pcm_remain], &mp3)) > 0) {
	void* channels[2];
	size_t stride[2] = {1, 1};
	int16_t *lptr = pcm_l;
	int16_t *rptr = pcm_r;
	char dst_buffer[BYTES_PER_PACKET];
	acast_t* dst;

	num_frames += pcm_remain;

	dst = (acast_t*) dst_buffer;
	dst->param = sparam;
	
	while(num_frames >= frames_per_packet) {
	    channels[0] = (void*) lptr;
	    channels[1] = (void*) rptr;

	    switch(chan_ctx.type) {
	    case ACAST_MAP_PERMUTE:
	    case ACAST_MAP_ID:
		permute_ni(sparam.format,
			   channels, sparam.channels_per_frame,
			   dst->data, num_output_channels,
			   chan_ctx.channel_map,
			   frames_per_packet);
		break;
	    case ACAST_MAP_OP:
		scatter_gather_ni(sparam.format,
				  channels, stride,
				  dst->data, num_output_channels,
				  chan_ctx.channel_op, chan_ctx.num_channel_ops,
				  frames_per_packet);
		break;
	    default:
		break;
	    }

	    lptr += frames_per_packet;
	    rptr += frames_per_packet;	    
	    
	    dst->seqno = seqno++;
	    dst->num_frames = frames_per_packet;
	    
	    if ((verbose > 3) && (seqno % 100 == 0)) {
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
	    num_frames -= frames_per_packet;
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
