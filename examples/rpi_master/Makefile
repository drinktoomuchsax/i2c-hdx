CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I ../../src

DEMO = i2c_hdx_master_demo
TOOL = i2c_hdx_master_tool

PORT_SRCS = i2c_hdx_port_linux.c ../../src/i2c_hdx_master.c

DEMO_SRCS = main.c $(PORT_SRCS)
TOOL_SRCS = i2c_hdx_tool.c $(PORT_SRCS)

.PHONY: all clean

all: $(DEMO) $(TOOL)

$(DEMO): $(DEMO_SRCS)
	$(CC) $(CFLAGS) -o $@ $^

$(TOOL): $(TOOL_SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(DEMO) $(TOOL)
