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

static inline void swap(uint8_t* ptr1, uint8_t* ptr2)
{
    uint8_t t = *ptr1;
    *ptr1 = *ptr2;
    *ptr2 = t;
}

static inline void swap16(void* data)
{
    swap((uint8_t*)data, (uint8_t*)data+1);
}

static inline void swap32(void* data)
{
    swap((uint8_t*)data, (uint8_t*)data+3);
    swap((uint8_t*)data+1, (uint8_t*)data+2);
}


static inline void little16(void* data)
{
#if __BYTE_ORDER == __BIG_ENDIAN
    swap16(data);
#endif
}

static inline void little32(void* data)
{
#if __BYTE_ORDER == __BIG_ENDIAN
    swap32(data);
#endif
}

static uint32_t read_u32le(int fd)
{
    uint32_t x;
    if (read(fd, &x, 4) == 4) {
	little32((uint8_t*)&x);
	return x;
    }
    return 0;
}

static int read_tag(int fd, uint32_t* tag)
{
    int n;
    if ((n = read(fd, tag, 4)) == 4) {
	swap32((uint8_t*)tag);
	fprintf(stderr, "tag = %c%c%c%c\n",
		(*tag>>24)&0xff,(*tag>>16)&0xff,
		(*tag>>8)&0xff,*tag&0xff);
    }
    return n;
}

static int wav_decode_init(int fd, wav_header_t* hdr, xwav_header_t* xhdr,
			   uint32_t* num_frames)
{
    int       header_found = 0;
    int       data_found = 0;
    int       tries = 20;
    uint32_t  tag;
    uint32_t  taglen; 
    uint32_t  data_length = 0;
    uint32_t  file_length = 0;

    if (read_tag(fd,&tag) < 4) return -1;
    if (tag != WAV_ID_RIFF) return -1;
    file_length = read_u32le(fd);
    fprintf(stderr, "file_length = %u\n", file_length);
    if (read_tag(fd,&tag) < 4) return -1;
    if (tag != WAV_ID_WAVE) return -1;

again:
    tries--;
    if (tries < 0) return -1;
    if (read_tag(fd, &tag) < 4) return -1;
    if (tag == WAV_ID_FMT) {
	taglen = (read_u32le(fd)+1) & -2;
	fprintf(stderr, "taglen = %u\n", taglen);
	if (taglen < sizeof(wav_header_t)) return -1;	    

	if (read(fd, hdr, sizeof(wav_header_t)) != sizeof(wav_header_t))
	    return -1;
	little16(&hdr->AudioFormat);
	little16(&hdr->NumChannels);
	little32(&hdr->SampleRate);
	little32(&hdr->ByteRate);
	little16(&hdr->BlockAlign);
	little16(&hdr->BitsPerChannel);
	taglen -= sizeof(wav_header_t);
	
	/* WAVE_FORMAT_EXTENSIBLE support */
	if ((taglen >= sizeof(xwav_header_t)) &&
	    (hdr->AudioFormat == WAVE_FORMAT_EXTENSIBLE)) {
	    if (xhdr) {
		if (read(fd,xhdr,sizeof(xwav_header_t))!=sizeof(xwav_header_t))
		    return -1;
		little16(&xhdr->cbSize);
		little16(&xhdr->ValidBitsPerChannel);
		little32(&xhdr->ChannelMask);
		little16(&xhdr->AudioFormat);
		taglen -= sizeof(xwav_header_t);
		hdr->AudioFormat = xhdr->AudioFormat;
	    }
	    else {
		if (lseek(fd, sizeof(xwav_header_t), SEEK_CUR) < 0)
		    return -1;
		taglen -= sizeof(xwav_header_t);
	    }
	}
	if (taglen > 0) {
	    if (lseek(fd, (long) taglen, SEEK_CUR) < 0)
		return -1;
	}
	header_found = 1;
	goto again;
    }
    else if (tag == WAV_ID_DATA) {
	taglen = read_u32le(fd);
	fprintf(stderr, "taglen = %u\n", taglen);
	data_length = taglen;
	data_found = 1;
    }
    else {
	// skip this section
	taglen = (read_u32le(fd)+1) & -2;
	fprintf(stderr, "taglen = %u\n", taglen);	
	if (lseek(fd, (long) taglen, SEEK_CUR) < 0)
	    return -1;
	goto again;
    }

    if (header_found && data_found) {
        if (hdr->AudioFormat == 0x0050 || hdr->AudioFormat == 0x0055) {
            return 3;
        }
	switch(hdr->AudioFormat) {
	case WAVE_FORMAT_PCM:
	case WAVE_FORMAT_IEEE_FLOAT:
	case WAVE_FORMAT_ALAW:
	case WAVE_FORMAT_ULAW:
	    break;
	default:
            if (verbose) {
		fprintf(stderr, "unsupported data format: 0x%04X\n",
			hdr->AudioFormat);
	    }
            return 0;
        }	
        if (data_length == MAX_U_32_NUM)
	    *num_frames = MAX_U_32_NUM;
	else 
            *num_frames = data_length/(hdr->NumChannels*(hdr->BitsPerChannel+7)/8);
        return 1;
    }
    return -1;
}

int wav_decode(int fd, uint8_t* buf, wav_header_t* hdr,
	       snd_pcm_uframes_t frames_per_packet)
{
    uint32_t bytes_per_frame = wav_get_bytes_per_frame(hdr);
    uint32_t n = frames_per_packet * bytes_per_frame;
    int r;

    if ((r = read(fd, buf, n)) < 0)
	return r;
    return r / bytes_per_frame;
}
    

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

void wav_print(wav_header_t* ptr)
{
    fprintf(stderr, "AudioFormat=%04x\n", ptr->AudioFormat);
    fprintf(stderr, "NumChannels=%d\n", ptr->NumChannels);
    fprintf(stderr, "SampleRate=%d\n", ptr->SampleRate);
    fprintf(stderr, "ByteRate=%d\n", ptr->ByteRate);
    fprintf(stderr, "BlockAlign=%d\n", ptr->BlockAlign);
    fprintf(stderr, "BitsPerChannel=%d\n", ptr->BitsPerChannel);
}

void xwav_print(xwav_header_t* ptr)
{
    fprintf(stderr, "cbSize=%d\n", ptr->cbSize);
    fprintf(stderr, "ValidBitsPerChannel=%d\n", ptr->ValidBitsPerChannel);
    fprintf(stderr, "ChannelMask=%x\n", ptr->ChannelMask);
    fprintf(stderr, "AudioFormat=%04x\n", ptr->AudioFormat);
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
    int        use_channel_map = 0;  // prefer simple map if possible
    int        id_channel_map = 0;   // one to one map
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
    
    { // try build a simple channel_map
	int i;
	int max_dst_channel = -1;

	use_channel_map = 1;
	id_channel_map = 1;
	
	for (i = 0; (i < num_channel_ops) && (i < MAX_CHANNEL_MAP); i++) {
	    if (channel_op[i].dst > max_dst_channel)
		max_dst_channel = channel_op[i].dst;
	    if ((channel_op[i].dst != i) ||
		(channel_op[i].op != ACAST_OP_SRC1)) {
		use_channel_map = 0;
		id_channel_map = 0;
	    }
	    else if (use_channel_map) {
		channel_map[i] = channel_op[i].src1;
		if (channel_map[i] != i)
		    id_channel_map = 0;
	    }
	}
	if (i >= MAX_CHANNEL_MAP)
	    use_channel_map = 0;
	
	if (num_output_channels == 0)
	    num_output_channels = max_dst_channel+1;
	if (num_output_channels != wav.NumChannels)
	    id_channel_map = 0;
    }

    if (verbose) {
	printf("Channel map: ");
	print_channel_ops(channel_op, num_channel_ops);
	printf("use_channel_map: %d\n", use_channel_map);
	printf("id_channel_map: %d\n", id_channel_map);
	printf("num_output_channels = %d\n", num_output_channels);
    }

    if (verbose > 1) {
	wav_print(&wav);
	if (wav.AudioFormat == xwav.AudioFormat)
	    xwav_print(&xwav);
    }

    switch(wav.AudioFormat) {
    case WAVE_FORMAT_PCM:
	switch(wav.BitsPerChannel) {
	case 8:  fmt = SND_PCM_FORMAT_U8; break;
	case 16: fmt = SND_PCM_FORMAT_S16_LE; break;
	case 32: fmt = SND_PCM_FORMAT_S32_LE; break;
	default:
	    fprintf(stderr, "unsupport wav file format\n");
	    exit(1);
	}
	break;
    case WAVE_FORMAT_IEEE_FLOAT:
	switch(wav.BitsPerChannel) {
	case 32: fmt = SND_PCM_FORMAT_FLOAT_LE; break;
	case 64: fmt = SND_PCM_FORMAT_FLOAT64_LE; break;
	default:
	    fprintf(stderr, "unsupport wav file format\n");
	    exit(1);
	}
	break;
    case WAVE_FORMAT_ALAW:
	fmt = SND_PCM_TYPE_ALAW;
	break;
    case WAVE_FORMAT_ULAW:
	fmt = SND_PCM_TYPE_MULAW;
	break;
    default:
	fmt = SND_PCM_FORMAT_S16_LE;
	break;
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
	fprintf(stderr, "  snd_bytes_per_frame:%ld\n",
		snd_bytes_per_frame);
	fprintf(stderr, "  snd_frames_per_packet:%ld\n",
		snd_frames_per_packet);
	fprintf(stderr, "----------------\n");
    }
    silence =  (acast_t*) silence_buffer;
    silence->num_frames = snd_frames_per_packet;
    snd_pcm_format_set_silence(sparam.format, silence->data,
			       snd_frames_per_packet*snd_bytes_per_frame);
    acast_play(handle, snd_bytes_per_frame,
	       silence->data, silence->num_frames);
    acast_play(handle, snd_bytes_per_frame,
	       silence->data, silence->num_frames);
    snd_pcm_start(handle);	

    frames_per_packet = min(snd_frames_per_packet,wav_frames_per_packet);

    bytes_per_frame = num_output_channels * sparam.bytes_per_channel;

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

	if (use_channel_map) {
	    if (id_channel_map) {
		dst = src;
		dst->param = sparam;
	    }
	    else {
		dst = (acast_t*) dst_buffer;
		dst->param = sparam;
		map_channels(sparam.format,
			     src->data, wav.NumChannels,
			     dst->data, num_output_channels, 
			     channel_map,
			     frames_per_packet);
	    }
	}
	else {
	    dst = (acast_t*) dst_buffer;
	    dst->param = sparam;
	    op_channels(sparam.format,
			src->data, wav.NumChannels,
			dst->data, num_output_channels,
			channel_op, num_channel_ops,
			frames_per_packet);
	}
	if (num_frames < MAX_U_32_NUM)
	    num_frames -= n;
	dst->seqno = seqno++;
	dst->num_frames = n;
	if ((verbose > 3) && (dst->seqno % 100 == 0)) {
	    acast_print(stderr, dst);
	}
	acast_play(handle, bytes_per_frame, dst->data, dst->num_frames);
    }
    exit(0);
}
