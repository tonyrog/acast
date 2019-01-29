// acast_info list cards/devices/formats
#include <stdio.h>
#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

void card_info(int card)
{
    int err;
    char  ctl_name[80];
    // char* name;
    // char* longname;
    snd_ctl_t* ctl;
    snd_ctl_card_info_t* info;

    printf("CARD %d {\n", card);
    sprintf(ctl_name, "hw:%d", card);
    if ((err=snd_ctl_open(&ctl, ctl_name, 0)) < 0) {
	fprintf(stderr, "snd_ctl_open:%s\n", snd_strerror(err));
	return;
    }
    snd_ctl_card_info_malloc(&info);
    
    snd_ctl_card_info(ctl, info);

    printf("  id: \"%s\",\n", snd_ctl_card_info_get_id(info));
    printf("  driver:\"%s\",\n", snd_ctl_card_info_get_driver(info));
    printf("  name:\"%s\",\n", snd_ctl_card_info_get_name(info));
    printf("  longname:\"%s\",\n", snd_ctl_card_info_get_longname(info));
    printf("  mixername:\"%s\",\n", snd_ctl_card_info_get_mixername(info));
    printf("  components:\"%s\"\n", snd_ctl_card_info_get_components(info));

    snd_ctl_card_info_free(info);

    printf("}\n");
    
    snd_ctl_close(ctl);    
}

int main(int argc, char** argv)
{
    int totalCards = 0;   // No cards found yet
    int cardNum = -1;     // Start with first card

    while(1) {
	int err;
        // Get next sound card's card number.
        if ((err = snd_card_next(&cardNum)) < 0) {
            fprintf(stderr, "Can't get the next card number: %s\n",
                            snd_strerror(err));
            break;
        }
        if (cardNum < 0)
            // No more cards
            break;
	card_info(cardNum);
        ++totalCards;   // Another card found, so bump the count
    }
    printf("ALSA found %i card(s)\n", totalCards);  
}
