
all: acast_sender acast_receiver

acast_sender:	acast_sender.o  acast.o
	$(CC) -o$@ acast_sender.o acast.o -lasound


acast_receiver:	acast_receiver.o acast.o
	$(CC) -o$@ acast_receiver.o  acast.o -lasound

acast_receiver.o: acast.h
acast_sender.o: acast.h
acast.o: acast.h
