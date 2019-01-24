CFLAGS = -Og
LDFLAGS = -g


all: acast_sender acast_receiver mp3_sender acast_info

acast_sender:	acast_sender.o  acast.o
	$(CC) -o$@ $(LDFLAGS) acast_sender.o acast.o -lasound

mp3_sender:	mp3_sender.o acast.o
	$(CC) -o$@ $(LDFLAGS) mp3_sender.o acast.o -lmp3lame -lasound

acast_receiver:	acast_receiver.o acast.o
	$(CC) -o$@ $(LDFLAGS) acast_receiver.o  acast.o -lasound


acast_info: acast_info.o
	$(CC) -o$@ acast_info.o -lasound

acast_receiver.o: acast.h
acast_sender.o: acast.h
acast.o: acast.h
