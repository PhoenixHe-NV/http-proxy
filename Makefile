CC = clang
#CFLAGS = -pthread -O2
CFLAGS = -pthread -g -O0

all: proxy.c csapp.c
	cd core && make
	$(CC) $(CFLAGS) -o proxy proxy.c csapp.c core/proxy_core.a

clean:
	cd core && make clean
	rm -f proxy *~ *.o tmp/*

