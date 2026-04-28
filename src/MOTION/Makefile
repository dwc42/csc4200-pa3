CC      = gcc
CFLAGS  = -Wall -Wextra -g -I../../include
LDFLAGS = 
STUB    ?= 0
WIRINGPI_LIBS ?= -lwiringPi

ifeq ($(STUB),1)
	CFLAGS += -DLED_STUB
else
	LDFLAGS += $(WIRINGPI_LIBS)
endif

SOURCES = motion_test.c motion.c
OBJECTS = $(SOURCES:.c=.o)
TARGET  = motion_test

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
