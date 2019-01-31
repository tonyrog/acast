CFLAGS = -Og  -Wall
LDFLAGS = -g

OBJS =  acast.o wav.o g711.o tick.o
MOBJS = $(OBJS) mp3.o

all: acast_sender acast_receiver mp3_sender mp3_player wav_sender wav_player acast_info

acast_sender:	acast_sender.o $(OBJS)
	$(CC) -o$@ $(LDFLAGS) acast_sender.o $(OBJS) -lasound

mp3_sender:	mp3_sender.o $(MOBJS)
	$(CC) -o$@ $(LDFLAGS) mp3_sender.o $(MOBJS) -lmp3lame -lasound

mp3_player:	mp3_player.o $(MOBJS)
	$(CC) -o$@ $(LDFLAGS) mp3_player.o $(MOBJS) -lmp3lame -lasound

wav_sender:	wav_sender.o $(OBJS)
	$(CC) -o$@ $(LDFLAGS) wav_sender.o $(OBJS) -lasound

wav_player:	wav_player.o $(OBJS)
	$(CC) -o$@ $(LDFLAGS) wav_player.o $(OBJS) -lasound

acast_receiver:	acast_receiver.o $(OBJS)
		$(CC) -o$@ $(LDFLAGS) acast_receiver.o $(OBJS) -lasound

acast_info: acast_info.o
	$(CC) -o$@ acast_info.o -lasound

acast_receiver.o: acast.h
acast_sender.o: acast.h tick.h
acast.o: acast.h g711.h
wav_player.o: acast.h tick.h wav.h
wav_sender.o: acast.h tick.h wav.h
mp3_sender.o: acast.h tick.h mp3.h
mp3_player.o: acast.h tick.h mp3.h
wav.o:	wav.h
mp3.o:	mp3.h
g711.o:	g711.h
