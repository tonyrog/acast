//
//  mp3_sender
//
//     open a mp3 file and read data samples
//     and multicast over ip (ttl = 1)
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <lame/lame.h>

#include <stdio.h>
#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#include "acast.h"

#define DEBUG 1

#define MAX_U_32_NUM    0xFFFFFFFF
#define MP3BUFFER_SIZE  4096
#define MP3TAG_SIZE     4096
#define PCM_BUFFER_SIZE 1152


int is_mp1 = 0;
int is_mp2 = 0;
int is_mp3 = 0;
int in_id3v2_size = -1;
uint8_t* in_id3v2_tag = NULL;
int silent = 1;

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


int setup_multicast(char* maddr, char* ifaddr, int mport,
			int ttl, int loop,
			struct sockaddr_in* addr, int* addrlen)
{
    int s;
    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
	uint32_t laddr = inet_addr(ifaddr);
	
	bzero((char *)addr, sizeof(*addr));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = inet_addr(maddr);
	addr->sin_port = htons(mport);
	
	setsockopt(s,IPPROTO_IP,IP_MULTICAST_IF,(void*)&laddr,sizeof(laddr));
	setsockopt(s,IPPROTO_IP,IP_MULTICAST_TTL,(void*)&ttl, sizeof(ttl));
	setsockopt(s,IPPROTO_IP,IP_MULTICAST_LOOP,(void*)&loop, sizeof(loop));	

	*addrlen = sizeof(*addr);
    }
    return s;
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
        if (silent < 9) {
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
        if (silent < 9) {
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
        if (silent < 10) {
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

int mp3_decode(int fd, hip_t hip, short pcm_l[], short pcm_r[],
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


#define S_ERR(name) do { snd_function_name = #name; goto snd_error; } while(0)

int main(int argc, char** argv)
{
    char* name = "default";
    char* filename;
    snd_pcm_t *handle;
    acast_params_t iparam;
    acast_params_t bparam;
    snd_pcm_uframes_t frames_per_packet;    
    uint32_t seqno = 0;
    char* snd_function_name = "unknown";
    int err, tmp;
    int s;
    struct sockaddr_in addr;
    int addrlen;
    char acast_buffer[BYTES_PER_PACKET];
    acast_t* acast;
    int multicast_loop = 1;   // loopback multicast packets
    int multicast_ttl  = 0;   // ttl=0 local host, ttl=1 local network
    hip_t hip;
    mp3data_struct mp3;
    short pcm_l[PCM_BUFFER_SIZE];
    short pcm_r[PCM_BUFFER_SIZE];
    char* ptr;
    int fd, len, n, ret;
    int enc_delay, enc_padding;
    int play = 1;
    uint8_t channel_map[6] = {0,1,0,1,0,0};
    
    if (argc < 1) {
	fprintf(stderr, "usage: mp3_sender <file>.mp3\n");
	exit(1);
    }
    filename = argv[1];
    if ((fd = open(filename, O_RDONLY)) < 0) {
	fprintf(stderr, "error: unable to open %s: %s\n",
		filename, strerror(errno));
	exit(1);
    }

    memset(acast_buffer, 0, sizeof(acast_buffer));
    acast = (acast_t*) acast_buffer;
    acast->seqno = 0;
    acast->num_frames = 0;
    acast_clear_param(&acast->param);
    
    if ((hip = hip_decode_init()) == NULL) {
	perror("hip_decode_init");
	exit(1);
    }

    hip_set_msgf(hip, silent < 10 ? emit_message : 0);
    hip_set_errorf(hip, silent < 10 ? emit_error : 0);
    hip_set_debugf(hip, emit_debug);

    ret = mp3_decode_init(fd, hip, &mp3, &enc_delay, &enc_padding);

#ifdef DEBUG
    fprintf(stderr, "header_parsed=%d\n", mp3.header_parsed);
    fprintf(stderr, "channels=%d\n", mp3.stereo);
    fprintf(stderr, "samplerate=%d\n",  mp3.samplerate);
    fprintf(stderr, "framesize=%d\n", mp3.framesize);
    fprintf(stderr, "bitrate=%d\n", mp3.bitrate);
    fprintf(stderr, "totalframes=%u\n", mp3.totalframes);
    fprintf(stderr, "nsamp=%lu\n", mp3.nsamp);
    fprintf(stderr, "mode=%d\n", mp3.mode);
    fprintf(stderr, "enc_delay=%d\n", enc_delay);
    fprintf(stderr, "enc_padding=%d\n", enc_padding);
#endif
    
    if (play) {
	if ((err=snd_pcm_open(&handle,name,SND_PCM_STREAM_PLAYBACK,0)) < 0)
	    S_ERR(snd_pcm_open);
	acast_clear_param(&iparam);
	// setup wanted paramters
	iparam.format = SND_PCM_FORMAT_S16_LE;
	iparam.sample_rate = mp3.samplerate;
	iparam.channels_per_frame = mp3.stereo;
	acast_setup_param(handle, &iparam, &bparam, &frames_per_packet);
	acast_print_params(stderr, &bparam);
	acast->param = bparam;
    }

    while((len = mp3_decode(fd, hip, pcm_l, pcm_r, &mp3)) > 0) {
	void* channels[2];
	int16_t *lptr = pcm_l;
	int16_t *rptr = pcm_r;

	while(len > 0) {
	    int flen = (len >= frames_per_packet) ? frames_per_packet : len;
	    channels[0] = (void*) lptr;
	    channels[1] = (void*) rptr;
	
	    interleave_channels(bparam.format, channels,
				acast->param.channels_per_frame,
				acast->data,
				channel_map,
				flen);
	    
	    acast->seqno = seqno++;
	    acast->num_frames = flen;
	    if ((silent < 2) && (acast->seqno % 100 == 0)) {
		acast_print(stderr, acast);
	    }

	    if (play) {
		snd_pcm_writei(handle, acast->data,
			       (snd_pcm_uframes_t)acast->num_frames);
	    }
	    // sendto
	    len -= flen;
	    lptr += flen;
	    rptr += flen;
	}
    }
    
    hip_decode_exit(hip);

    exit(0);
snd_error:
    fprintf(stderr, "sound error %s %s\n",
	    snd_function_name, snd_strerror(err));
    exit(1);
}
