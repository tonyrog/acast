#include "acast_file.h"

acast_file_t* acast_file_open(char* filename, int mode)
{
    acast_file_t* af = NULL;
    int len = strlen(filename);
    char* xptr = filename + len;

    if (strcasecmp(xptr - 4, ".wav") == 0) {
	if ((af = wav_file_open(filename, mode)) != NULL)
	    return af;
    }
    else if (strcasecmp(xptr - 4, ".mp3") == 0) {
	if ((af = mp3_file_open(filename, mode)) != NULL)
	    return af;
    }
    else {
	goto try_open;
    }
    
    if (errno == ENOENT)
	return NULL;

try_open:
    if ((af = wav_file_open(filename, mode)) != NULL)
	return af;
    if ((af = mp3_file_open(filename, mode)) != NULL)
	return af;
    return NULL;
}
