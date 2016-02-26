CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -levent -lpthread

OBJS = proxy.o csapp.o libevent_proxy.o

all: proxy

proxy: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

csapp.o: csapp.c
	$(CC) $(CFLAGS) -c csapp.c

proxy.o: proxy.c
	$(CC) $(CFLAGS) -c proxy.c

libevent_proxy.o: libevent_proxy.c
	$(CC) $(CFLAGS) -c libevent_proxy.c

clean:
	rm -f *~ *.o libevent_exapmle

