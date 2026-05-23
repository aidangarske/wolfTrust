all: firmware-stm32h563

clean:
	rm -f wolftrust
	$(MAKE) -C tests/firmware/stm32h563 clean

.PHONY: all clean firmware-stm32h563 run-stm32h563 run-stm32h563-tui run-stm32h563-uarts

firmware-stm32h563:
	$(MAKE) -C tests/firmware/stm32h563 all

run-stm32h563:
	$(MAKE) -C tests/firmware/stm32h563 run

run-stm32h563-tui:
	$(MAKE) -C tests/firmware/stm32h563 run-tui

run-stm32h563-uarts:
	$(MAKE) -C tests/firmware/stm32h563 run-uarts
