# Host-side test of the desk drive logic (no ESP-IDF needed):  make -C test
CC ?= gcc
CFLAGS ?= -std=gnu17 -Wall -Wextra -O2
BUILD := ../build-test

.PHONY: test clean

test: $(BUILD)/desk_sim
	$(BUILD)/desk_sim

$(BUILD)/desk_sim: desk_sim.c ../main/desk_logic.c ../main/desk_logic.h ../main/config.h stub/tusb.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Istub -I../main desk_sim.c ../main/desk_logic.c -lm -o $@

clean:
	rm -rf $(BUILD)
