#include <motion.h>

void motionDetectEventCallback()
{
	printf("Motion Detected");
}
int main()
{
	setupGPIO();
	subscribeMotionDetectEvent(motionDetectEventCallback);
}