
CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall -Werror -Wextra

.PHONY: all clean 

.DEFAULT: all

all: aesdsocket

aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) -I/. $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o aesdsocket
