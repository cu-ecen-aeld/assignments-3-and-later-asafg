
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror -Wextra
LDFLAGS ?= -pthread -lrt
INCLUDES ?= -I/.
TARGET ?= aesdsocket

.PHONY: all clean 

.DEFAULT: all

all: $(TARGET)

$(TARGET): itimer_thread.o sock_thread.o main.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDES) $^ -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $<

clean:
	rm -f *.o $(TARGET)
