CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -levent -lpthread

OBJS = proxy.o csapp.o cache.o 

all: proxy

proxy: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

csapp.o: csapp.c
	$(CC) $(CFLAGS) -c csapp.c

proxy.o: proxy.c
	$(CC) $(CFLAGS) -c proxy.c

cache.o: cache.c
	$(CC) $(CFLAGS) -c cache.c


clean:
	rm -f *~ *.o echo

