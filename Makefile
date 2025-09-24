CC = gcc
CFLAGS = -Wall -O2

all: server

server: server.c
	$(CC) server.c -o server

clean:
	rm -f server
