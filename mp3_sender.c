//
//  mp3_sender
//
//     open a mp3 file and read data samples
//     and multicast over ip (ttl = 1)
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

#define PLAYBACK_DEVICE "default"
// ttl=0 local host, ttl=1 local network
#define MULTICAST_TTL  1
#define MULTICAST_LOOP 0
#define NUM_CHANNELS   2
#define CHANNEL_MAP   "01"

#define MAX_U_32_NUM    0xFFFFFFFF
#define MP3BUFFER_SIZE  4096
#define MP3TAG_SIZE     4096
#define PCM_BUFFER_SIZE 1152

int in_id3v2_size = -1;
uint8_t* in_id3v2_tag = NULL;

int verbose = 0;
int debug = 0;

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

int check_aid(uint8_t* header)
{
    return 0 == memcmp(header, "AiD\1", 4);
}

int lenOfId3v2Tag(uint8_t* buf)
{
    unsigned int b0 = buf[0] & 127;
    unsigned int b1 = buf[1] & 127;
    unsigned int b2 = buf[2] & 127;
    unsigned int b3 = buf[3] & 127;
    return (((((b0 << 7) + b1) << 7) + b2) << 7) + b3;
}

int is_syncword_mp123(uint8_t* p, int level)
{
    static const uint8_t abl2[16] =
	{ 0, 7, 7, 7, 0, 7, 0, 0,
	  0, 0, 0, 8, 8, 8, 8, 8 };

    if ((p[0] & 0xFF) != 0xFF)
        return 0;       /* first 8 bits must be '1' */
    if ((p[1] & 0xE0) != 0xE0)
        return 0;       /* next 3 bits are also */
    if ((p[1] & 0x18) == 0x08)
        return 0;       /* no MPEG-1, -2 or -2.5 */
    switch (p[1] & 0x06) {
    default:
    case 0x00:         /* illegal Layer */
        return 0;
    case 0x02:         /* Layer3 */
        if (level != 3) return 0;
        break;
    case 0x04:         /* Layer2 */
	if (level != 2) return 0;
        break;
    case 0x06:         /* Layer1 */
	if (level != 1) return 0;	
        break;
    }
    if ((p[1] & 0x06) == 0x00)
        return 0;       /* no Layer I, II and III */
    if ((p[2] & 0xF0) == 0xF0)
        return 0;       /* bad bitrate */
    if ((p[2] & 0x0C) == 0x0C)
        return 0;       /* no sample frequency with (32,44.1,48)/(1,2,4)     */
    if ((p[1] & 0x18) == 0x18 && (p[1] & 0x06) == 0x04 && abl2[p[2] >> 4] & (1 << (p[3] >> 6)))
        return 0;
    if ((p[3] & 3) == 2)
        return 0;       /* reserved enphasis mode */
    return 1;
}


int mp3_decode_init(int fd, hip_t hip, mp3data_struct* mp,
		    int* enc_delay, int* enc_padding)
{
    uint8_t buf[100];
    int     ret;
    size_t  len;
    int     aid_header;
    int16_t pcm_l[1152], pcm_r[1152];
    int     freeformat = 0;

sync:
    memset(mp, 0, sizeof(mp3data_struct));
    len = 4;
    if (read(fd, buf, len) != len)
        return -1;      /* failed */
    while (buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {
        len = 6;
        if (read(fd, &buf[4], len) != len)
            return -1;  /* failed */
        len = lenOfId3v2Tag(&buf[6]);
        if (in_id3v2_size < 1) {
            in_id3v2_size = 10 + len;
            in_id3v2_tag = malloc(in_id3v2_size);
            if (in_id3v2_tag) {
                memcpy(in_id3v2_tag, buf, 10);
                if (read(fd, &in_id3v2_tag[10], len) != len)
                    return -1;  /* failed */
                len = 0; /* copied, nothing to skip */
            }
            else {
                in_id3v2_size = 0;
            }
        }
	lseek(fd, (long) len, SEEK_CUR);
        len = 4;
        if (read(fd, &buf, len) != len)
            return -1;  /* failed */
    }
    aid_header = check_aid(buf);
    if (aid_header) {
        if (read(fd, &buf, 2) != 2)
            return -1;  /* failed */
        aid_header = (uint8_t) buf[0] + 256 * (uint8_t) buf[1];
        if (verbose >= 1) {
            fprintf(stderr,"Album ID found.  length=%i \n", aid_header);
        }
        /* skip rest of AID, except for 6 bytes we have already read */
        lseek(fd, aid_header - 6, SEEK_CUR);

        /* read 4 more bytes to set up buffer for MP3 header check */
        if (read(fd, &buf, len) != len)
            return -1;  /* failed */
    }
    len = 4;
    while (!is_syncword_mp123(buf, 3)) {
        unsigned int i;
        for (i = 0; i < len - 1; i++)
            buf[i] = buf[i + 1];
        if (read(fd, buf + len - 1, 1) != 1)
            return -1;  /* failed */
    }

    if ((buf[2] & 0xf0) == 0) {
        if (verbose) {
            fprintf(stderr,"Input file is freeformat.\n");
        }
        freeformat = 1;
    }
    
    /* now parse the current buffer looking for MP3 headers.    */
    /* (as of 11/00: mpglib modified so that for the first frame where  */
    /* headers are parsed, no data will be decoded.   */
    /* However, for freeformat, we need to decode an entire frame, */
    /* so mp3data->bitrate will be 0 until we have decoded the first */
    /* frame.  Cannot decode first frame here because we are not */
    /* yet prepared to handle the output. */
    ret = hip_decode1_headersB(hip, buf, len, pcm_l, pcm_r,
			       mp, enc_delay, enc_padding);
    if (ret == -1)
        return -1;

    /* repeat until we decode a valid mp3 header.  */
    while (!mp->header_parsed) {
        len = read(fd, buf, sizeof(buf));
        if (len != sizeof(buf))
            return -1;
        ret =
            hip_decode1_headersB(hip, buf, len, pcm_l, pcm_r,
				 mp, enc_delay, enc_padding);
        if (ret == -1)
            return -1;
    }

    if (mp->bitrate == 0 && !freeformat) {
        if (verbose) {
            fprintf(stderr, "fail to sync...\n");
        }
	goto sync;
    }

    if (mp->totalframes > 0) {
        /* mpglib found a Xing VBR header and computed nsamp & totalframes */
    }
    else {
        /* set as unknown.  Later, we will take a guess based on file size
         * ant bitrate */
        mp->nsamp = (uint32_t)(-1);
    }
    return 0;
}

int mp3_decode(int fd, hip_t hip, int16_t* pcm_l, int16_t* pcm_r,
	       mp3data_struct *mp)
{
    int     ret = 0;
    size_t  len = 0;
    uint8_t buf[1024];

    // first see if we still have data buffered in the decoder
    ret = hip_decode1_headers(hip, buf, len, pcm_l, pcm_r, mp);
    while(ret == 0) {
        len = read(fd, buf, 1024);
        if (len == 0) {
            ret = hip_decode1_headers(hip, buf, len, pcm_l, pcm_r, mp);
	    if (ret == 0) { errno = 0; return -1; }
	    return ret;
        }
        ret = hip_decode1_headers(hip, buf, len, pcm_l, pcm_r, mp);
    }
    return ret;
}

void mp3_print(mp3data_struct* ptr)
{
    fprintf(stderr, "header_parsed=%d\n", ptr->header_parsed);
    fprintf(stderr, "channels=%d\n",      ptr->stereo);
    fprintf(stderr, "samplerate=%d\n",    ptr->samplerate);
    fprintf(stderr, "framesize=%d\n",     ptr->framesize);
    fprintf(stderr, "bitrate=%d\n",       ptr->bitrate);
    fprintf(stderr, "totalframes=%u\n",   ptr->totalframes);
    fprintf(stderr, "nsamp=%lu\n",        ptr->nsamp);
    fprintf(stderr, "mode=%d\n",          ptr->mode);
}

void help(void)
{
printf("usage: mp3_sender [options] file\n"
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
    mp3data_struct mp3;
    int16_t pcm_l[2*PCM_BUFFER_SIZE];
    int16_t pcm_r[2*PCM_BUFFER_SIZE];
    int pcm_remain;
    char* ptr;
    int fd, len, n, ret;
    int num_frames;
    int enc_delay, enc_padding;
    int do_play = 0;
    int do_send = 1;
    int num_output_channels = NUM_CHANNELS;
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
    
    if ((hip = hip_decode_init()) == NULL) {
	perror("hip_decode_init");
	exit(1);
    }

    hip_set_msgf(hip, verbose ? emit_message : 0);
    hip_set_errorf(hip, verbose ? emit_error : 0);
    hip_set_debugf(hip, emit_debug);

    ret = mp3_decode_init(fd, hip, &mp3, &enc_delay, &enc_padding);
    if (verbose > 1) {
	mp3_print(&mp3);
	fprintf(stderr, "enc_delay=%d\n", enc_delay);
	fprintf(stderr, "enc_padding=%d\n", enc_padding);
    }

    fmt = SND_PCM_FORMAT_S16_LE;
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
	bparam.sample_rate = mp3.samplerate;
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
	iparam.sample_rate = mp3.samplerate;
	iparam.channels_per_frame = mp3.stereo;
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
    
    while((num_frames = mp3_decode(fd, hip,
				   &pcm_l[pcm_remain],
				   &pcm_r[pcm_remain], &mp3)) > 0) {
	void* channels[2];
	int16_t *lptr = pcm_l;
	int16_t *rptr = pcm_r;

	num_frames += pcm_remain;
	
	while(num_frames >= frames_per_packet) {
	    channels[0] = (void*) lptr;
	    channels[1] = (void*) rptr;
	
	    interleave_channels(bparam.format, channels,
				acast->param.channels_per_frame,
				acast->data,
				channel_map,
				frames_per_packet);

	    lptr += frames_per_packet;
	    rptr += frames_per_packet;	    
	    
	    acast->seqno = seqno++;
	    acast->num_frames = frames_per_packet;
	    if ((verbose > 3) && (acast->seqno % 100 == 0)) {
		acast_print(stderr, acast);
	    }
	    if (do_send) {
		size_t nbytes = bytes_per_frame*acast->num_frames;
		if (sendto(s, acast_buffer, sizeof(acast_t)+nbytes, 0,
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
