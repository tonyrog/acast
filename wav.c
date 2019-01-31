#include <stdint.h>
#include "wav.h"

#define MAX_U_32_NUM    0xFFFFFFFF

snd_pcm_format_t wav_to_snd(uint16_t format, int bits_per_channel)
{
    switch(format) {
    case WAVE_FORMAT_PCM:
	switch(bits_per_channel) {
	case 8:  return SND_PCM_FORMAT_U8;
	case 16: return SND_PCM_FORMAT_S16_LE;
	case 32: return SND_PCM_FORMAT_S32_LE;
	default: return SND_PCM_FORMAT_UNKNOWN;
	}
    case WAVE_FORMAT_IEEE_FLOAT:
	switch(bits_per_channel) {
	case 32: return SND_PCM_FORMAT_FLOAT_LE; 
	case 64: return SND_PCM_FORMAT_FLOAT64_LE;
	default:  return SND_PCM_FORMAT_UNKNOWN;
	}
    case WAVE_FORMAT_ALAW: return SND_PCM_TYPE_ALAW;
    case WAVE_FORMAT_ULAW: return SND_PCM_TYPE_MULAW;
    default: return SND_PCM_FORMAT_UNKNOWN;
    }
}

// return -1 file format error
//         0 ok
//         1 compressed (num_frames invalid)
// 
int wav_decode_init(int fd, wav_header_t* hdr, xwav_header_t* xhdr,
		    uint32_t* num_frames)
{
    int       header_found = 0;
    int       data_found = 0;
    int       tries = 20;
    uint32_t  tag;
    uint32_t  taglen; 
    uint32_t  data_length = 0;
    // uint32_t  file_length = 0;

    if (read_tag(fd,&tag) < 4) return -1;
    if (tag != WAV_ID_RIFF) return -1;
    (void) read_u32le(fd);  // file_length
    if (read_tag(fd,&tag) < 4) return -1;
    if (tag != WAV_ID_WAVE) return -1;

again:
    tries--;
    if (tries < 0) return -1;
    if (read_tag(fd, &tag) < 4) return -1;
    if (tag == WAV_ID_FMT) {
	taglen = (read_u32le(fd)+1) & -2;
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
	data_length = taglen;
	data_found = 1;
    }
    else {
	// skip this section
	taglen = (read_u32le(fd)+1) & -2;
	if (lseek(fd, (long) taglen, SEEK_CUR) < 0)
	    return -1;
	goto again;
    }

    if (header_found && data_found) {
        if (hdr->AudioFormat == 0x0050 || hdr->AudioFormat == 0x0055) {
            return 1;
        }
        if (data_length == MAX_U_32_NUM)
	    *num_frames = MAX_U_32_NUM;
	else 
            *num_frames = data_length/(hdr->NumChannels*(hdr->BitsPerChannel+7)/8);
        return 0;
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
    
void wav_print(FILE* f, wav_header_t* ptr)
{
    fprintf(f, "AudioFormat=%04x\n", ptr->AudioFormat);
    fprintf(f, "NumChannels=%d\n", ptr->NumChannels);
    fprintf(f, "SampleRate=%d\n", ptr->SampleRate);
    fprintf(f, "ByteRate=%d\n", ptr->ByteRate);
    fprintf(f, "BlockAlign=%d\n", ptr->BlockAlign);
    fprintf(f, "BitsPerChannel=%d\n", ptr->BitsPerChannel);
}

void xwav_print(FILE* f, xwav_header_t* ptr)
{
    fprintf(f, "cbSize=%d\n", ptr->cbSize);
    fprintf(f, "ValidBitsPerChannel=%d\n", ptr->ValidBitsPerChannel);
    fprintf(f, "ChannelMask=%x\n", ptr->ChannelMask);
    fprintf(f, "AudioFormat=%04x\n", ptr->AudioFormat);
}