# use /dev/aesdchar as the output file
# instead of /var/tmp/aesdsocketdata.
# no time stamps writer timer when using /dev/aesdchar . 
USE_AESD_CHAR_DEVICE ?= -DUSE_AESD_CHAR_DEVICE=1

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror -Wextra $(USE_AESD_CHAR_DEVICE)
LDFLAGS ?= -pthread -lrt
INCLUDES ?= -I/.
TARGET ?= aesdsocket

.PHONY: all clean 

.DEFAULT: all

all: $(TARGET)

$(TARGET): itimer_thread.o sock_thread.o main.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $<

clean:
	rm -f *.o $(TARGET)
