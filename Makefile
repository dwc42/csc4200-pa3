CC      = gcc
CFLAGS  = -Wall -Wextra -g -Iinclude
LDFLAGS = -lcrypto
SAN_FLAGS = -fsanitize=address -fno-omit-frame-pointer
STUB ?= 0
LED_LIBS ?= -lgpiod

ifeq ($(STUB),1)
	CFLAGS += -DLED_STUB
else
	LDFLAGS += $(LED_LIBS)
endif

# Override these when invoking make, e.g.:
# make leak-lightserver SERVER_ARGS="-p 5000 -l lightserver.log"
# make leak-lightclient CLIENT_ARGS="-s 127.0.0.1 -p 5000 -l lightclient.log -f input.txt"
SERVER_ARGS ?=
CLIENT_ARGS ?=

OBJ_DIR = obj
SRC     = src

.PHONY: all clean lightserver-asan lightclient-asan leak-lightserver leak-lightclient leak led motion

all: lightserver lightclient led motion
led:
	$(MAKE) -C $(SRC)/LED

motion:
	$(MAKE) -C $(SRC)/MOTION


$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/protocol.o: $(SRC)/protocol.c include/protocol.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/led.o: $(SRC)/LED/led.c $(SRC)/LED/led.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

lightserver: $(SRC)/lightserver.c $(OBJ_DIR)/protocol.o $(OBJ_DIR)/led.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

lightclient: $(SRC)/lightclient.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

lightserver-asan: $(SRC)/lightserver.c $(OBJ_DIR)/protocol.o $(OBJ_DIR)/led.o
	$(CC) $(CFLAGS) $(SAN_FLAGS) $^ -o $@ $(LDFLAGS) $(SAN_FLAGS)

lightclient-asan: $(SRC)/lightclient.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $(SAN_FLAGS) $^ -o $@ $(LDFLAGS) $(SAN_FLAGS)

leak-lightserver: lightserver-asan
	ASAN_OPTIONS=detect_leaks=1 ./lightserver-asan $(SERVER_ARGS)

leak-lightclient: lightclient-asan
	ASAN_OPTIONS=detect_leaks=1 ./lightclient-asan $(CLIENT_ARGS)

leak: leak-lightserver leak-lightclient

clean:
	rm -rf $(OBJ_DIR) lightserver lightclient lightserver-asan lightclient-asan received_* *.log
	$(MAKE) -C $(SRC)/LED clean
	$(MAKE) -C $(SRC)/MOTION clean
