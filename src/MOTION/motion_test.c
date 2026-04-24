#include "motion.h"

void motionDetectEventCallback()
{
	printf("Motion Detected\n");
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