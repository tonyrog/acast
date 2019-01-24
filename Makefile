CFLAGS = -Og
LDFLAGS = -g

<<<<<<< HEAD
all: acast_sender acast_receiver mp3_sender
=======
all: acast_sender acast_receiver acast_info
>>>>>>> 5665d0cd4d33c85e102b42fc2d52ce8cb75f39a3

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
