CC=gcc
CFLAGS=-Wall -g

all: oss user

util.o: util.c util.h
	$(CC) $(CFLAGS) -c util.c

queue.o: queue.c queue.h
	$(CC) $(CFLAGS) -c queue.c

oss: oss.c oss.h queue.o util.o
	$(CC) $(CFLAGS) oss.c queue.o util.o -o oss -lrt

user: user.c oss.h
	$(CC) $(CFLAGS) -DDEBUG user.c -o user

clean:
	rm -f oss user *.log *.o
