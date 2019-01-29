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

#define PLAYBACK_DEVICE "default"
#define NUM_CHANNELS   2

#define CHANNEL_MAP   "auto"
#define MAX_CHANNEL_OP  16
#define MAX_CHANNEL_MAP 8

#define MAX_U_32_NUM    0xFFFFFFFF
#define MP3BUFFER_SIZE  4096
#define MP3TAG_SIZE     4096
#define PCM_BUFFER_SIZE 1152

int in_id3v2_size = -1;
uint8_t* in_id3v2_tag = NULL;

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

    if (strcmp(map, "auto") == 0) {
	int i, n;

	n = (num_output_channels == 0) ? mp3.stereo : num_output_channels;
	for (i = 0; i < n; i++) {
	    channel_op[i].op = ACAST_OP_SRC1;
	    channel_op[i].src1 = i % mp3.stereo;
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
	if (num_output_channels != mp3.stereo)
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
	mp3_print(&mp3);
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

	    if (use_channel_map) {
		interleave_channels(sparam.format,
				    channels, sparam.channels_per_frame,
				    dst->data, channel_map,
				    frames_per_packet);
	    }
	    else {
		iop_channels(sparam.format,
			     channels, 2,
			     dst->data, num_output_channels,
			     channel_op, num_channel_ops,
			     frames_per_packet);
	    }

	    lptr += frames_per_packet;
	    rptr += frames_per_packet;	    
	    
	    dst->seqno = seqno++;
	    dst->num_frames = frames_per_packet;
	    
	    if ((verbose > 3) && (seqno % 100 == 0)) {
		acast_print(stderr, dst);
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
