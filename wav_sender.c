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

#define PLAYBACK_DEVICE "default"
// ttl=0 local host, ttl=1 local network
#define MULTICAST_TTL  1
#define MULTICAST_LOOP 0
#define NUM_CHANNELS   2
#define CHANNEL_MAP   "01"

#define MAX_U_32_NUM 0xFFFFFFFF

int verbose = 0;
int debug = 0;

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

static int read_i16le(int fd)
{
    int16_t x;
    if (read(fd, &x, 2) == 2) {
	little16((uint8_t*)&x);
	return x;
    }
    return -1;
}

static int read_u16le(int fd)
{
    uint16_t x;
    if (read(fd, &x, 2) == 2) {
	little16((uint8_t*)&x);
	return x;
    }
    return -1;
}


static int read_i32le(int fd)
{
    int32_t x;
    if (read(fd, &x, 4) == 4) {
	little32((uint8_t*)&x);
	return x;
    }
}

static uint32_t read_u32le(int fd)
{
    uint32_t x;
    if (read(fd, &x, 4) == 4) {
	little32((uint8_t*)&x);
	return x;
    }
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

/* AIFF Definitions */

static uint32_t const IFF_ID_FORM = 0x464f524d; /* "FORM" */
static uint32_t const IFF_ID_AIFF = 0x41494646; /* "AIFF" */
static uint32_t const IFF_ID_AIFC = 0x41494643; /* "AIFC" */
static uint32_t const IFF_ID_COMM = 0x434f4d4d; /* "COMM" */
static uint32_t const IFF_ID_SSND = 0x53534e44; /* "SSND" */
static uint32_t const IFF_ID_MPEG = 0x4d504547; /* "MPEG" */
static uint32_t const IFF_ID_NONE = 0x4e4f4e45; /* "NONE" *//* AIFF-C data format */
static uint32_t const IFF_ID_2CBE = 0x74776f73; /* "twos" *//* AIFF-C data format */
static uint32_t const IFF_ID_2CLE = 0x736f7774; /* "sowt" *//* AIFF-C data format */
static uint32_t const WAV_ID_RIFF = 0x52494646; /* "RIFF" */
static uint32_t const WAV_ID_WAVE = 0x57415645; /* "WAVE" */
static uint32_t const WAV_ID_FMT = 0x666d7420;  /* "fmt " */
static uint32_t const WAV_ID_DATA = 0x64617461; /* "data" */

static uint16_t const WAVE_FORMAT_PCM = 0x0001;
static uint16_t const WAVE_FORMAT_IEEE_FLOAT = 0x0003;
static uint16_t const WAVE_FORMAT_ALAW = 0x0006;
static uint16_t const WAVE_FORMAT_ULAW = 0x0007;
static uint16_t const WAVE_FORMAT_EXTENSIBLE = 0xFFFE;

/*****************************************************************************
 *
 *	Read Microsoft Wave headers
 *
 *	By the time we get here the first 32-bits of the file have already been
 *	read, and we're pretty sure that we're looking at a WAV file.
 *
 *****************************************************************************/

typedef struct
{
    uint16_t AudioFormat;
    uint16_t NumChannels;
    uint32_t SampleRate;
    uint32_t ByteRate;
    uint16_t BlockAlign;
    uint16_t BitsPerChannel;
} wav_header_t;

typedef struct
{
    uint16_t cbSize;
    uint16_t ValidBitsPerChannel;
    uint32_t ChannelMask;
    uint16_t AudioFormat;    
} xwav_header_t;
    

static int wav_decode_init(int fd, wav_header_t* hdr, xwav_header_t* xhdr,
			   uint32_t* num_frames)
{
    int       header_found = 0;
    int       data_found = 0;
    uint32_t  subSize = 0;
    int       tries = 20;
    uint32_t  tag;
    uint32_t  taglen; 
    uint32_t  type;
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
        if ((hdr->AudioFormat != WAVE_FORMAT_PCM) &&
	    (hdr->AudioFormat != WAVE_FORMAT_IEEE_FLOAT)) {
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
    uint32_t bytes_per_frame = (hdr->NumChannels*(hdr->BitsPerChannel+7)/8);
    uint32_t n = frames_per_packet * bytes_per_frame;
    int r;

    if ((r = read(fd, buf, n)) < 0)
	return r;
    return r / bytes_per_frame;
}
    

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
    acast_params_t bparam;
    snd_pcm_uframes_t frames_per_packet;    
    uint32_t seqno = 0;
    int err, tmp;
    int s;
    char acast_buffer[BYTES_PER_PACKET];
    acast_t* acast;
    char* multicast_addr = MULTICAST_ADDR;
    char* multicast_ifaddr = MULTICAST_IFADDR;    // interface address
    uint16_t multicast_port = MULTICAST_PORT;
    int multicast_loop = MULTICAST_LOOP;   // loopback multicast packets
    int multicast_ttl  = MULTICAST_TTL;
    struct sockaddr_in addr;
    int addrlen;
    hip_t hip;
    snd_pcm_format_t fmt;
    wav_header_t wav;
    xwav_header_t xwav;
    int pcm_remain;
    char* ptr;
    int fd, len, n, ret;
    int num_frames;
    int enc_delay, enc_padding;
    int do_play = 0;
    int do_send = 1;
    int num_output_channels = 0;
    uint8_t channel_map[8] = {0,1,0,1,0,0,0,0};
    size_t bytes_per_frame;
    size_t network_bufsize = 4*BYTES_PER_PACKET;
    tick_t last_time;
    tick_t first_time;
    tick_t send_time;
    uint64_t frame_delay_us;  // delay per frame in us
    uint64_t sent_frames;     // number of frames sent
    int mode = 0; // SND_PCM_NONBLOCK;
    
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
	    {"device", required_argument, 0, 'd'},
	    {"channels",required_argument, 0, 'c'},
	    {"map",     required_argument, 0, 'm'},	    
	    {0,        0,                 0, 0}
	};
	
	c = getopt_long(argc, argv, "lhvDa:i:p:t:d:c:m:",
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
	case 'm': {
	    char* ptr = optarg;
	    int i = 0;
	    while((i < 8) && isdigit(*ptr)) {
		channel_map[i++] = (*ptr-'0');
	    }
	    break;
	}	    
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
    
    memset(acast_buffer, 0, sizeof(acast_buffer));
    acast = (acast_t*) acast_buffer;
    acast->seqno = 0;
    acast->num_frames = 0;
    acast_clear_param(&acast->param);

    if ((ret = wav_decode_init(fd, &wav, &xwav, &num_frames)) < 0) {
	fprintf(stderr, "no wav data found\n");
	exit(1);
    }
    if (num_output_channels == 0)
	num_output_channels = wav.NumChannels;

    if (verbose > 1) {
	wav_print(&wav);
	if (wav.AudioFormat == xwav.AudioFormat)
	    xwav_print(&xwav);
    }

    if (wav.AudioFormat == WAVE_FORMAT_PCM) {
	switch(wav.BitsPerChannel) {
	case 8:  fmt = SND_PCM_FORMAT_U8; break;
	case 16: fmt = SND_PCM_FORMAT_S16_LE; break;
	case 32: fmt = SND_PCM_FORMAT_S32_LE; break;
	default:
	    fprintf(stderr, "unsupport wav file format\n");
	    exit(1);
	}
    }
    else if (wav.AudioFormat == WAVE_FORMAT_IEEE_FLOAT) {
	switch(wav.BitsPerChannel) {
	case 32: fmt = SND_PCM_FORMAT_FLOAT_LE; break;
	case 64: fmt = SND_PCM_FORMAT_FLOAT64_LE; break;
	default:
	    fprintf(stderr, "unsupport wav file format\n");
	    exit(1);
	}
    }
    
    sent_frames = 0;

    if (do_send) {
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
	bparam.format = fmt;
	bparam.sample_rate = wav.SampleRate;
	bparam.channels_per_frame = num_output_channels;
	bparam.bits_per_channel = snd_pcm_format_width(fmt);
	bparam.bytes_per_channel = snd_pcm_format_physical_width(fmt) / 8;
	bytes_per_frame = bparam.bytes_per_channel*bparam.channels_per_frame;
	frames_per_packet = acast_get_frames_per_packet(&bparam);
	frame_delay_us = (frames_per_packet*1000000) / bparam.sample_rate;
    }
    
    if (do_play) {
	if ((err=snd_pcm_open(&handle,playback_device_name,
			      SND_PCM_STREAM_PLAYBACK,mode)) < 0) {
	    fprintf(stderr, "snd_pcm_open failed %s\n", snd_strerror(err));
	    exit(1);
	}
	acast_clear_param(&iparam);
	// setup wanted paramters
	iparam.format = fmt;
	iparam.sample_rate = wav.SampleRate;
	iparam.channels_per_frame = wav.NumChannels;
	acast_setup_param(handle, &iparam, &bparam, &frames_per_packet);
	bytes_per_frame = bparam.bytes_per_channel*bparam.channels_per_frame;
    }
    
    if (verbose > 1) {
	acast_print_params(stderr, &bparam);
	fprintf(stderr, "frames_per_packet=%ld\n", frames_per_packet);
    }
    
    acast->param = bparam;

    pcm_remain = 0;  // samples that remain from last round
    last_time = time_tick_now();
    
    while((num_frames==MAX_U_32_NUM) || (num_frames >= frames_per_packet)) {
	if ((n = wav_decode(fd,acast->data,&wav,frames_per_packet)) < 0) { 
	    fprintf(stderr, "read error: %s\n", strerror(errno));
	    exit(1);
	}
	// fixme map channels
	if (num_frames < MAX_U_32_NUM)
	    num_frames -= n;
	acast->seqno = seqno++;
	acast->num_frames = n;
	if ((verbose > 3) && (acast->seqno % 100 == 0)) {
	    acast_print(stderr, acast);
	}
	if (do_send) {
	    size_t nbytes = bytes_per_frame*acast->num_frames;
	    if (sendto(s,acast_buffer, sizeof(acast_t)+nbytes, 0,
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
	}
	if (do_play) {
	    acast_play(handle, bytes_per_frame,
		       acast->data, acast->num_frames);
	}
	if (do_send && !do_play) {
	    last_time = time_tick_wait_until(send_time +
					     frame_delay_us-enc_delay);
	    // send_time is the absolute send time mark
	    send_time += frame_delay_us;
	}
	num_frames -= frames_per_packet;
    }
    exit(0);
}
