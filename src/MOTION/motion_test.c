#include "motion.h"

void motionDetectEventCallback()
{
	printf("Motion Detected");
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