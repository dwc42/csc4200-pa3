#include "motion.h"
bool enableMotionDetectedInterrupt();
bool disableMotionDetectedInterrupt();

void (*motionEventCallbacks[MAX_MOTION_EVENT_CALLBACKS])(void);
uint16_t motionEventCallbacksLength = 0;

int8_t currentPin = -1;

void onMotionDetectedInternal(void)
{
	for (uint16_t i = 0; i < motionEventCallbacksLength; i++)
	{
		motionEventCallbacks[i]();
	}
}
bool motionEnabled = false;
bool setPin(uint8_t pin)
{
	if (motionEnabled)
	{
		if (!disableMotionDetectedInterrupt())
			return false;
	}
	currentPin = pin;
	if (motionEnabled)
	{
		if (!enableMotionDetectedInterrupt())
			return false;
	}
	return true;
}
bool enableMotionDetectedInterrupt()
{
	if (motionEnabled)
		return false;
	pinMode(currentPin, INPUT);
	pullUpDnControl(currentPin, PUD_UP); // Enable internal pull-up

	// Register ISR for falling edge on GPIO 16
	if (wiringPiISR(currentPin, INT_EDGE_FALLING, &onMotionDetectedInternal) < 1)
	{
		return false;
	};
	motionEnabled = true;
	return true;
}
bool disableMotionDetectedInterrupt()
{
	if (!motionEnabled)
		return true;
	wiringPiISRStop(currentPin);
	motionEnabled = false;
	return true;
}

bool subscribeMotionDetectEvent(MotionDetectEventCallback callback)
{
	if (motionEventCallbacksLength >= MAX_MOTION_EVENT_CALLBACKS)
		return false;
	motionEventCallbacks[motionEventCallbacksLength] = callback;
	motionEventCallbacksLength++;
	if (!motionEnabled && !enableMotionDetectedInterrupt())
		return false;
	return true;
}
bool unsubscribeMotionDetectEvent(MotionDetectEventCallback callback)
{
	if (motionEventCallbacksLength <= 0)
		return false;
	if (motionEventCallbacksLength == 1 && motionEventCallbacks[0] == callback)
	{

		motionEventCallbacksLength = 0;
		if (motionEnabled && !disableMotionDetectedInterrupt())
			return false;
		return true;
	}
	int16_t indexFound = -1;
	for (uint16_t i = 0; i < motionEventCallbacksLength; i++)
	{
		if (motionEventCallbacks[i] == callback)
		{
			indexFound = i;
			break;
		}
	}
	if (indexFound == -1)
		return false;
	for (uint16_t i = indexFound; i < motionEventCallbacksLength - 1; i++)
	{
		motionEventCallbacks[i] = motionEventCallbacks[i + 1];
	}
	--motionEventCallbacksLength;
	return true;
}

bool setupGPIO()
{
	if (wiringPiSetupGpio() == -1)
		return false;
	return true;
}