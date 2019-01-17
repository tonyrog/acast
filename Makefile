
acast_sender:	acast_sender.o
	$(CC) -o$@ acast_sender.o -lasound
