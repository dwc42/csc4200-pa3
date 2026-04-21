#include "./led.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	char *end = NULL;
	long blinkCount;
	long onMs;
	long offMs;

	if (argc != 4)
	{
		fprintf(stderr, "Usage: %s <count> <on_ms> <off_ms>\n", argv[0]);
		return 1;
	}

	errno = 0;
	blinkCount = strtol(argv[1], &end, 10);
	if (errno != 0 || end == argv[1] || *end != '\0' || blinkCount <= 0 || blinkCount > 65535)
	{
		fprintf(stderr, "Invalid count: %s\n", argv[1]);
		return 1;
	}

	errno = 0;
	onMs = strtol(argv[2], &end, 10);
	if (errno != 0 || end == argv[2] || *end != '\0' || onMs <= 0 || onMs > 65535)
	{
		fprintf(stderr, "Invalid on_ms: %s\n", argv[2]);
		return 1;
	}

	errno = 0;
	offMs = strtol(argv[3], &end, 10);
	if (errno != 0 || end == argv[3] || *end != '\0' || offMs <= 0 || offMs > 65535)
	{
		fprintf(stderr, "Invalid off_ms: %s\n", argv[3]);
		return 1;
	}

	Led *led = led_create(17);
	if (!led)
		return 1;
	int pid = fork();
	if (!pid)
	{
		if (led_blink(led, (uint16_t)blinkCount, (uint16_t)onMs, (uint16_t)offMs, 0) < 0)
		{
			led_destroy(led);
			return 1;
		}
		while (led_busy(led))
			usleep(10000);

		led_destroy(led);
	}
	return 0;
}