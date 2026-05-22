CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Iinclude

SRCS = \
	src/main.c \
	src/monitor.c \
	src/partitions.c \
	src/platform_stub.c

OBJS = $(SRCS:.c=.o)

all: wolftrust

wolftrust: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(OBJS) wolftrust

.PHONY: all clean

firmware-stm32h563:
	$(MAKE) -C firmware/stm32h563 all

run-stm32h563:
	$(MAKE) -C firmware/stm32h563 run

run-stm32h563-uarts:
	$(MAKE) -C firmware/stm32h563 run-uarts
