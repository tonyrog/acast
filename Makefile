CFLAGS = -Og  -Wall
LDFLAGS = -g

OBJS =  acast_channel.o acast_file.o acast.o wav.o g711.o tick.o mp3.o
LIBS = -lmp3lame -lasound

all: acast_sender acast_receiver afile_sender afile_player acast_info

acast_sender:	acast_sender.o $(OBJS)
	$(CC) -o$@ $(LDFLAGS) acast_sender.o $(OBJS) $(LIBS)

afile_sender:	afile_sender.o $(OBJS)
	$(CC) -o$@ $(LDFLAGS) afile_sender.o $(OBJS) $(LIBS)

afile_player:	afile_player.o $(OBJS)
	$(CC) -o$@ $(LDFLAGS) acast_player.o $(OBJS) $(LIBS)

acast_receiver:	acast_receiver.o $(OBJS)
		$(CC) -o$@ $(LDFLAGS) acast_receiver.o $(OBJS) $(LIBS)

acast_info: acast_info.o
	$(CC) -o$@ acast_info.o -lasound

acast_receiver.o: acast.h
acast_sender.o: acast.h tick.h
acast_channel.o: acast_channel.h
acast.o: acast.h g711.h acast_channel.h
afile_player.o: acast.h acast_file.h tick.h 
afile_sender.o: acast.h acast_file.h tick.h
acast_file.h:	acast.h
wav.o:	wav.h
mp3.o:	mp3.h
g711.o:	g711.h
