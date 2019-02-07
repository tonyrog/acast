//
//  wav_sender
//
//     open a wav file and play data samples
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <endian.h>
#include <ctype.h>
#include <getopt.h>
#include <sys/types.h>

#include <lame/lame.h>

#include "acast.h"
#include "tick.h"
#include "wav.h"

#define PLAYBACK_DEVICE "default"
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
printf("usage: wav_player [options] file\n"
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
    snd_pcm_uframes_t wav_frames_per_packet;
    snd_pcm_uframes_t snd_frames_per_packet;
    snd_pcm_uframes_t frames_per_packet;
    size_t snd_bytes_per_frame;
    size_t bytes_per_frame;
    uint32_t seqno = 0;
    int play_started = 0;
    int err;
    snd_pcm_format_t fmt;
    wav_header_t wav;
    xwav_header_t xwav;
    int fd, n, ret;
    uint32_t num_frames;
    int num_output_channels = 0;
    char* map = CHANNEL_MAP;
    acast_op_t channel_op[MAX_CHANNEL_OP];
    uint8_t    channel_map[MAX_CHANNEL_MAP];
    int        num_channel_ops;
    char       silence_buffer[BYTES_PER_PACKET];
    acast_t*   silence;
    int mode = 0; // SND_PCM_NONBLOCK;
    int map_type; // 0=id, 1=map, 2=op
    
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

    if (verbose > 1) {
	wav_print(stderr, &wav);
	if (wav.AudioFormat == xwav.AudioFormat)
	    xwav_print(stderr, &xwav);
	fprintf(stderr, "  wav_frames_per_packet = %lu\n",
		wav_frames_per_packet);	
    }
    

	
    // normally we want to acast 6 channels but now we select the
    // native wav format for testing

    if (strcmp(map, "auto") == 0) {
	int i, n;

	n = (num_output_channels == 0) ? wav.NumChannels : num_output_channels;
	for (i = 0; i < n; i++) {
	    channel_op[i].op = ACAST_OP_SRC1;
	    channel_op[i].src1 = i % wav.NumChannels;
	    channel_op[i].src2 = 0;
	    channel_op[i].dst = i;
	}
	num_channel_ops = n;
    }
    else {
	if ((num_channel_ops = parse_channel_ops(map, channel_op,
						 MAX_CHANNEL_OP)) < 0) {
	    fprintf(stderr, "map synatx error\n");
	    exit(1);
	}
    }

    if ((map_type = build_channel_map(channel_op, num_channel_ops,
				      channel_map, MAX_CHANNEL_MAP,
				      wav.NumChannels,
				      &num_output_channels)) < 0) {
	fprintf(stderr, "map synatx error\n");
	exit(1);
    }	
	
    if (verbose) {
	printf("Channel map: ");
	print_channel_ops(channel_op, num_channel_ops);
	printf("  use_channel_map = %d\n", (map_type > 0));
	printf("  id_channel_map = %d\n",  (map_type == 1));
	printf("  num_output_channels = %d\n", num_output_channels);
    }


    if ((fmt = wav_to_snd(wav.AudioFormat, wav.BitsPerChannel)) ==
	SND_PCM_FORMAT_UNKNOWN) {
	fprintf(stderr, "%04x unsupport wav file format\n", wav.AudioFormat);
	exit(1);
    }

    if ((err=snd_pcm_open(&handle,playback_device_name,
			  SND_PCM_STREAM_PLAYBACK,mode)) < 0) {
	fprintf(stderr, "snd_pcm_open failed %s\n", snd_strerror(err));
	exit(1);
    }
    acast_clear_param(&iparam);
    // setup wanted paramters
    iparam.format = fmt;
    iparam.sample_rate = wav.SampleRate;
    iparam.channels_per_frame = num_output_channels;
    acast_setup_param(handle, &iparam, &sparam, &snd_frames_per_packet);
    snd_bytes_per_frame =
	sparam.bytes_per_channel*sparam.channels_per_frame;
    
    if (verbose) {
	fprintf(stderr, "snd params:\n");
	acast_print_params(stderr, &sparam);
	fprintf(stderr, "  snd_bytes_per_frame = %d\n",
		snd_bytes_per_frame);
	fprintf(stderr, "  snd_frames_per_packet = %lu\n",
		snd_frames_per_packet);
    }
    
    silence =  (acast_t*) silence_buffer;
    silence->num_frames = snd_frames_per_packet;
    snd_pcm_format_set_silence(sparam.format, silence->data,
			       snd_frames_per_packet*
			       sparam.channels_per_frame);
    

    frames_per_packet = min(snd_frames_per_packet,wav_frames_per_packet);

    bytes_per_frame = num_output_channels * sparam.bytes_per_channel;

    if (verbose) {
	fprintf(stderr, "  num_output_channels = %d\n", num_output_channels);
	fprintf(stderr, "  bytes_per_frame = %d\n",  bytes_per_frame);
	fprintf(stderr, "  frames_per_packet = %lu\n", frames_per_packet);
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
	    dst->param = sparam;
	    map_channels(sparam.format,
			 src->data, wav.NumChannels,
			 dst->data, num_output_channels, 
			 channel_map,
			 frames_per_packet);
	    break;
	case 2:
	    dst = (acast_t*) dst_buffer;
	    dst->param = sparam;
	    op_channels(sparam.format,
			src->data, wav.NumChannels,
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
