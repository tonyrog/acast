CFLAGS = -Og  -Wall
LDFLAGS = -g


all: acast_sender acast_receiver mp3_sender mp3_player wav_sender wav_player acast_info

acast_sender:	acast_sender.o  acast.o
	$(CC) -o$@ $(LDFLAGS) acast_sender.o acast.o -lasound

mp3_sender:	mp3_sender.o acast.o tick.o
	$(CC) -o$@ $(LDFLAGS) mp3_sender.o acast.o tick.o -lmp3lame -lasound

mp3_player:	mp3_player.o acast.o tick.o
	$(CC) -o$@ $(LDFLAGS) mp3_player.o acast.o tick.o -lmp3lame -lasound

wav_sender:	wav_sender.o acast.o tick.o
	$(CC) -o$@ $(LDFLAGS) wav_sender.o acast.o tick.o -lasound

wav_player:	wav_player.o acast.o tick.o
	$(CC) -o$@ $(LDFLAGS) wav_player.o acast.o tick.o -lasound

acast_receiver:	acast_receiver.o acast.o
	$(CC) -o$@ $(LDFLAGS) acast_receiver.o  acast.o -lasound

acast_info: acast_info.o
	$(CC) -o$@ acast_info.o -lasound

acast_receiver.o: acast.h
acast_sender.o: acast.h
acast.o: acast.h
wav_sender.o: acast.h
mp3_sender.o: acast.h
