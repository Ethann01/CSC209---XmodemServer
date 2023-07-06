DEPENDENCIES = xmodemserver.h crc16.h
PORT = 51286
CFLAGS= -DPORT=\$(PORT) -g -Wall

all: xmodemserver client1
xmodemserver: xmodemserver.o crc16.o
        gcc ${FLAGS} -o $@ $^
client1: crc16.o client1.o
        gcc ${FLAGS} -o $@ $^
clean:
        rm -f *.o xmodemserver client1