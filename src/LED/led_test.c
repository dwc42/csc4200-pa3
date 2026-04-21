#include "./led.h"
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{

	printf("Number of arguments: %d\n", argc);
	if (argc != 3)
		return 1;
	long blinkCount = strtol(argv[1]);
	long onMs = strtol(argv[2]);
	long offMs = strtol(argv[3]);

	Led *led = led_create(17);
	if (!led)
		return 1;

	if (led_blink(led, blinkCount, onMs, offMs, 0) < 0)
	{
		led_destroy(led);
		return 1;
	}

	while (led_busy(led))
		usleep(10000);

	led_destroy(led);
	return 0;
}