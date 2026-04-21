#include "led.h"

int main()
{
	Led *led = led_create(17);
	led_blink(led, 10, 1000, 1000, 0);
}