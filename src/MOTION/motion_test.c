#include "motion.h"
int i = 0;
void motionDetectEventCallback()
{
	printf("Motion Detected, %d\n", i++);
}
int main()
{
	setupGPIO();
	setPin(27);
	subscribeMotionDetectEvent(motionDetectEventCallback);
	while (1)
	{
	}
}