# Makefile for CPE464 tcp and udp test code
# updated by Hugh Smith - April 2023

# all target makes UDP test code
# tcpAll target makes the TCP test code


CC= gcc
CFLAGS= -g -Wall
LIBS = 

OBJS = networks.o gethostbyname.o pollLib.o safeUtil.o buffer.o communication.o

#uncomment next two lines if your using sendtoErr() library
LIBS += libcpe464.2.21.a -lstdc++ -ldl
CFLAGS += -D__LIBCPE464_


all: server rcopy

server: server.c $(OBJS)
	$(CC) $(CFLAGS) -o server server.c $(OBJS) $(LIBS)

rcopy: rcopy.c $(OBJS)
	$(CC) $(CFLAGS) -o rcopy rcopy.c $(OBJS) $(LIBS)

.c.o:
	gcc -c $(CFLAGS) $< -o $@ 

cleano:
	rm -f *.o

clean:
	rm -f server rcopy *.o