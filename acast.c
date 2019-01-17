// utils

#include <stdio.h>

#include <stdio.h>
#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#include "acast.h"

void print_params(FILE* f, acast_params_t* param)
{
    fprintf(f, "format: %s\n", snd_pcm_format_name(param->format));
    fprintf(f, "channels_per_frame: %u\n", param->channels_per_frame);
    fprintf(f, "bits_per_channel: %u\n", param->bits_per_channel);
    fprintf(f, "bytes_per_channel: %u\n", param->bytes_per_channel);
    fprintf(f, "sample_rate: %u\n", param->sample_rate);
}

void print_acast(FILE* f, acast_t* acast)
{
    uint16_t* fp;

    fprintf(f, "seqno: %u\n", acast->seqno);
    fprintf(f, "num_frames: %u\n", acast->num_frames);    
    print_params(f, &acast->param);
    fprintf(f, "[frame_time = %.2fms]\n",
	    1000*(1.0/acast->param.sample_rate)*acast->num_frames);
    // print first frame only (debugging)
    fp = (int16_t*) acast->data;
    fprintf(f, "data: [%04x][%04x][%04x][%04x][%04x][%04x]\n",
	    fp[0], fp[1], fp[2], fp[3], fp[4], fp[5]);
}
