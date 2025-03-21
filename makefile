CC = g++

all:
	$(CC) udpserver.c -o server
	$(CC) udpclient_unix.c -o client


