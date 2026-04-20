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
#ifndef LED_H_
#define LED_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Led Led;

/* Acquire a BCM GPIO pin as an output and drive it LOW.
 * Returns NULL on failure (chip/line open, line request) with errno set
 * and a diagnostic on stderr. */
Led* led_create(int bcm_pin);

/* Preempt any running blink, drive LOW, release the line. Safe on NULL. */
void led_destroy(Led* led);

/* Fire-and-forget blink with threshold-gate preempt semantics.
 *
 * count    -- number of HIGH phases (>= 1)
 * on_ms    -- milliseconds HIGH per phase   (clamped to >= 1)
 * off_ms   -- milliseconds LOW between HIGH phases (clamped to >= 1)
 * priority -- 0..255, higher wins
 *
 * Returns:
 *    0 -- accepted, started fresh (no prior blink running)
 *    1 -- accepted, PREEMPTED a lower-priority running blink
 *    2 -- REJECTED, equal-or-higher priority blink already running
 *   -1 -- error (bad args, fork, timer setup); errno set
 */
int  led_blink(Led* led,
               uint16_t count,
               uint16_t on_ms,
               uint16_t off_ms,
               uint8_t  priority);

/* Forcibly stop any running blink (SIGUSR1 + blocking waitpid),
 * drive the line LOW. No-op if idle. */
void led_cancel(Led* led);

/* 1 if a blink child is alive, 0 otherwise. Reaps implicitly. */
int  led_busy(Led* led);

/* Non-blocking reap of any exited child. Safe to call anytime. */
void led_reap(Led* led);

#ifdef __cplusplus
}
#endif

#endif /* LED_H_ */
