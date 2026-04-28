/*
 * led.h
 * CSC4200 -- PA3: Detect a Person
 * David Leathers
 *
 * Fork+SIGALRM driven LED blinker with threshold-gate preempt priority.
 * C-linkable; safe to include from C or C++.
 *
 * Model:
 *   led_create()  -- acquires the BCM pin as an output via libgpiod, drives LOW.
 *   led_blink()   -- forks a child that does the blink loop; parent returns
 *                    immediately. Threshold-gate: strictly higher priority
 *                    preempts a running blink via SIGUSR1; equal/lower is
 *                    rejected.
 *   led_busy()    -- implicit non-blocking reap, then reports liveness.
 *   led_destroy() -- preempts any running blink, drives LOW, releases.
 *
 * The caller OWNS ticking led_busy() or led_reap() if it cares about
 * timely zombie cleanup; led_blink() also reaps implicitly on entry.
 *
 * Build with -DLED_STUB to compile a printf-only backend for off-Pi testing.
 */
#ifndef MOTION_H_
#define MOTION_H_

#include <stdint.h>
#include <stdbool.h>

#include <stdio.h>
#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#else
#include <wiringPi.h>
#endif
#ifdef __cplusplus
extern "C"
{
#endif

#define MAX_MOTION_EVENT_CALLBACKS 32
	typedef struct MotionEvent
	{
		void (*motionEventCallback)(void *aux);
		int16_t eventId;
		void *aux;
	} MotionEvent;
	typedef void MotionDetectEventCallback(void *aux);
	bool setPin(uint8_t pin);
	int16_t subscribeMotionDetectEvent(MotionDetectEventCallback callback, void *aux);
	bool unsubscribeMotionDetectEvent(int16_t eventId);
	bool setupGPIO();
#ifdef __cplusplus
}
#endif

#endif /* MOTION_H_ */
