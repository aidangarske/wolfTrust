CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc/port/stm32h563

SRCS = \
	src/main.c \
	src/monitor.c \
	src/port/stm32h563/partitions.c \
	src/platform_stub.c

OBJS = $(SRCS:.c=.o)

all: wolftrust

wolftrust: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(OBJS) wolftrust

.PHONY: all clean

firmware-stm32h563:
	$(MAKE) -C tests/firmware/stm32h563 all

run-stm32h563:
	$(MAKE) -C tests/firmware/stm32h563 run

run-stm32h563-uarts:
	$(MAKE) -C tests/firmware/stm32h563 run-uarts
