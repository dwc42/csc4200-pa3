#include "motion.h"
bool enableMotionDetectedInterrupt();
bool disableMotionDetectedInterrupt();

uint16_t motionEventCallbacksLength = 0;
MotionEvent motionEvents[16];
int8_t currentPin = -1;

void onMotionDetectedInternal(void)
{
	for (uint16_t i = 0; i < motionEventCallbacksLength; i++)
	{
		motionEvents[i].motionEventCallback(motionEvents[i].aux);
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
int16_t currentEventId = 0;
int16_t subscribeMotionDetectEvent(MotionDetectEventCallback callback, void *aux)
{
	if (motionEventCallbacksLength >= MAX_MOTION_EVENT_CALLBACKS)
		return -1;
	motionEvents[motionEventCallbacksLength].motionEventCallback = callback;
	motionEvents[motionEventCallbacksLength].eventId = currentEventId++;
	motionEvents[motionEventCallbacksLength].aux = aux;
	motionEventCallbacksLength++;
	if (!motionEnabled && !enableMotionDetectedInterrupt())
		return -1;
	return motionEvents[motionEventCallbacksLength].eventId;
}
bool unsubscribeMotionDetectEvent(int16_t eventId)
{
	if (motionEventCallbacksLength <= 0)
		return false;
	if (motionEventCallbacksLength == 1 && motionEvents[0].eventId == eventId)
	{

		motionEventCallbacksLength = 0;
		if (motionEnabled && !disableMotionDetectedInterrupt())
			return false;
		return true;
	}
	int16_t indexFound = -1;
	for (uint16_t i = 0; i < motionEventCallbacksLength; i++)
	{
		if (motionEvents[i].eventId == eventId)
		{
			indexFound = i;
			break;
		}
	}
	if (indexFound == -1)
		return false;
	for (uint16_t i = indexFound; i < motionEventCallbacksLength - 1; i++)
	{
		motionEvents[i] = motionEvents[i + 1];
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