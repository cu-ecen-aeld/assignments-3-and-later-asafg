
CC = gcc
CFLAGS = -g -Wall

.PHONY: all clean 

.DEFAULT: all

all: writer

writer: writer.o
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -I/. $< -o $@

%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o writer
