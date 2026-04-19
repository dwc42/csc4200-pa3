CC      = gcc
CFLAGS  = -Wall -Wextra -g -Iinclude
LDFLAGS = -lcrypto
SAN_FLAGS = -fsanitize=address -fno-omit-frame-pointer

# Override these when invoking make, e.g.:
# make leak-server SERVER_ARGS="-p 5000 -l server.log"
# make leak-client CLIENT_ARGS="-s 127.0.0.1 -p 5000 -l client.log -f input.txt"
SERVER_ARGS ?=
CLIENT_ARGS ?=

OBJ_DIR = obj
SRC     = src

.PHONY: all clean server-asan client-asan leak-server leak-client leak

all: server client

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/protocol.o: $(SRC)/protocol.c include/protocol.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

server: $(SRC)/lightserver.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

client: $(SRC)/lightclient.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

server-asan: $(SRC)/lightserver.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $(SAN_FLAGS) $^ -o $@ $(LDFLAGS) $(SAN_FLAGS)

client-asan: $(SRC)/lightclient.c $(OBJ_DIR)/protocol.o
	$(CC) $(CFLAGS) $(SAN_FLAGS) $^ -o $@ $(LDFLAGS) $(SAN_FLAGS)

leak-server: server-asan
	ASAN_OPTIONS=detect_leaks=1 ./server-asan $(SERVER_ARGS)

leak-client: client-asan
	ASAN_OPTIONS=detect_leaks=1 ./client-asan $(CLIENT_ARGS)

leak: leak-server leak-client

clean:
	rm -rf $(OBJ_DIR) server client server-asan client-asan received_* *.log
