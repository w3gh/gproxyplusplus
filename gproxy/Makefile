SHELL = /bin/sh
SYSTEM = $(shell uname)
C++ = g++
CC = gcc
DFLAGS =
OFLAGS = -O3
LFLAGS = -L. -L../bncsutil/src/bncsutil/ -lbncsutil -ldl -lcurses
CFLAGS =

ifeq ($(SYSTEM),Darwin)
DFLAGS += -D__APPLE__
OFLAGS += -flat_namespace
else
LFLAGS += -lrt
endif

ifeq ($(SYSTEM),FreeBSD)
DFLAGS += -D__FREEBSD__
endif

ifeq ($(SYSTEM),SunOS)
DFLAGS += -D__SOLARIS__
LFLAGS += -lresolv -lsocket -lnsl
endif

CFLAGS += $(OFLAGS) $(DFLAGS) -I. -I../bncsutil/src/

OBJS = bncsutilinterface.o bnet.o bnetprotocol.o commandpacket.o config.o gameprotocol.o gproxy.o gpsprotocol.o socket.o util.o
COBJS =
PROGS = ./gproxy++

all: $(OBJS) $(COBJS) $(PROGS)

./gproxy++: $(OBJS) $(COBJS)
	$(C++) -o ./gproxy++ $(OBJS) $(COBJS) $(LFLAGS)

clean:
	rm -f $(OBJS) $(COBJS) $(PROGS)

$(OBJS): %.o: %.cpp
	$(C++) -o $@ $(CFLAGS) -c $<

$(COBJS): %.o: %.c
	$(CC) -o $@ $(CFLAGS) -c $<

./gproxy++: $(OBJS) $(COBJS)

all: $(PROGS)

bncsutilinterface.o: gproxy.h util.h bncsutilinterface.h
bnet.o: gproxy.h util.h config.h socket.h commandpacket.h bncsutilinterface.h bnetprotocol.h bnet.h gameprotocol.h
bnetprotocol.o: gproxy.h util.h bnetprotocol.h
commandpacket.o: gproxy.h commandpacket.h
config.o: gproxy.h config.h
gameprotocol.o: gproxy.h util.h gameprotocol.h
gproxy.o: gproxy.h util.h config.h socket.h commandpacket.h bnetprotocol.h bnet.h gameprotocol.h gpsprotocol.h
gpsprotocol.o: gproxy.h util.h gpsprotocol.h
socket.o: gproxy.h util.h socket.h
util.o: gproxy.h util.h
