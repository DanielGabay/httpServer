server: server.o threadpool.o
	gcc server.o threadpool.o -o server -Wvla -g -Wall -lpthread

server.o: server.c
	gcc -c server.c

threadpool.o: threadpool.c threadpool.h
	gcc -c threadpool.c -lpthread
