
CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall

.PHONY: all clean 

.DEFAULT: all

all: writer

writer: writer.o
	$(CC) $(CFLAGS) -I/. $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o writer
