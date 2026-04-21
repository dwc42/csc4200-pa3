#include "./led.h"
#include <unistd.h>

int main()
{
	Led *led = led_create(17);
	if (!led)
		return 1;

	if (led_blink(led, 10, 100, 400, 0) < 0)
	{
		led_destroy(led);
		return 1;
	}

	while (led_busy(led))
		usleep(10000);

	led_destroy(led);
	return 0;
}