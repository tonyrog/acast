//
// mp3 lame wrapper
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <memory.h>

#include <lame/lame.h>
#include "mp3.h"

static int in_id3v2_size = -1;
static uint8_t* in_id3v2_tag = NULL;

static int check_aid(uint8_t* header)
{
    return 0 == memcmp(header, "AiD\1", 4);
}

static int lenOfId3v2Tag(uint8_t* buf)
{
    unsigned int b0 = buf[0] & 127;
    unsigned int b1 = buf[1] & 127;
    unsigned int b2 = buf[2] & 127;
    unsigned int b3 = buf[3] & 127;
    return (((((b0 << 7) + b1) << 7) + b2) << 7) + b3;
}

static int is_syncword_mp123(uint8_t* p, int level)
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


void mp3_print(FILE* f, mp3data_struct* ptr)
{
    fprintf(f, "header_parsed=%d\n", ptr->header_parsed);
    fprintf(f, "channels=%d\n",      ptr->stereo);
    fprintf(f, "samplerate=%d\n",    ptr->samplerate);
    fprintf(f, "framesize=%d\n",     ptr->framesize);
    fprintf(f, "bitrate=%d\n",       ptr->bitrate);
    fprintf(f, "totalframes=%u\n",   ptr->totalframes);
    fprintf(f, "nsamp=%lu\n",        ptr->nsamp);
    fprintf(f, "mode=%d\n",          ptr->mode);
}
