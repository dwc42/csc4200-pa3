CC      = gcc
CFLAGS  = -Wall -Wextra -g -I../../include
LDFLAGS = 
STUB    ?= 0

ifeq ($(STUB),1)
	CFLAGS += -DLED_STUB
else
	LDFLAGS += -lgpiod
endif

SOURCES = led_test.c led.c
OBJECTS = $(SOURCES:.c=.o)
TARGET  = led_test

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
