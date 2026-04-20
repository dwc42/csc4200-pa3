CC      = gcc
CFLAGS  = -Wall -Wextra -g -Iinclude
LDFLAGS = -lcrypto
SAN_FLAGS = -fsanitize=address -fno-omit-frame-pointer

# LED backend -- libgpiod on Pi, printf stub otherwise.
#   make           -- real libgpiod build (needs libgpiod-dev, links -lgpiod)
#   make STUB=1    -- off-Pi dry run, no hardware calls, no gpiod link
# NOTE: when switching between the two modes, run 'make clean' first --
# obj/led.o does not track the STUB variable and will otherwise be reused.
LED_CFLAGS  =
LED_LDFLAGS = -lgpiod
ifdef STUB
LED_CFLAGS  += -DLED_STUB
LED_LDFLAGS =
endif

# Override when invoking make, e.g.:
# make leak-lightserver SERVER_ARGS="-p 5000 -l lightserver.log"
# make leak-lightclient CLIENT_ARGS="-s 127.0.0.1 -p 5000 -l lightclient.log -f input.txt"
SERVER_ARGS ?=
CLIENT_ARGS ?=

OBJ_DIR = obj
SRC     = src

.PHONY: all clean lightserver-asan lightclient-asan leak-lightserver leak-lightclient leak led_demo

all: lightserver lightclient led_demo

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/protocol.o: $(SRC)/protocol.c include/protocol.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/led.o: $(SRC)/led.c include/led.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(LED_CFLAGS) -c $< -o $@

lightserver: $(SRC)/lightserver.c $(OBJ_DIR)/protocol.o $(OBJ_DIR)/led.o
	$(CC) $(CFLAGS) $(LED_CFLAGS) $^ -o $@ $(LDFLAGS) $(LED_LDFLAGS)

lightclient: $(SRC)/lightclient.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

led_demo: $(SRC)/led_demo.c $(OBJ_DIR)/led.o
	$(CC) $(CFLAGS) $(LED_CFLAGS) $^ -o $@ $(LED_LDFLAGS)

lightserver-asan: $(SRC)/lightserver.c $(OBJ_DIR)/protocol.o $(OBJ_DIR)/led.o
	$(CC) $(CFLAGS) $(LED_CFLAGS) $(SAN_FLAGS) $^ -o $@ $(LDFLAGS) $(LED_LDFLAGS) $(SAN_FLAGS)

lightclient-asan: $(SRC)/lightclient.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $(SAN_FLAGS) $^ -o $@ $(LDFLAGS) $(SAN_FLAGS)

leak-lightserver: lightserver-asan
	ASAN_OPTIONS=detect_leaks=1 ./lightserver-asan $(SERVER_ARGS)

leak-lightclient: lightclient-asan
	ASAN_OPTIONS=detect_leaks=1 ./lightclient-asan $(CLIENT_ARGS)

leak: leak-lightserver leak-lightclient

clean:
	rm -rf $(OBJ_DIR) lightserver lightclient led_demo lightserver-asan lightclient-asan received_* *.log
