
CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall -Werror -Wextra

.PHONY: all clean 

.DEFAULT: all

all: aesdsocket

aesdsocket: itimer_thread.o sock_thread.o aesdsocket.o
	$(CC) $(CFLAGS) -I/. $^ -o $@ -pthread -lrt

%.o: %.c
	$(CC) $(CFLAGS) -I/. -c $<

clean:
	rm -f *.o aesdsocket
