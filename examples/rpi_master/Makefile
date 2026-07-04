CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I ../../src

TARGET = i2c_hdx_master_demo

SRCS = main.c \
       i2c_hdx_port_linux.c \
       ../../src/i2c_hdx_master.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
