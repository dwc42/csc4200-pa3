/*
 * led.c
 * CSC4200 -- PA3: Detect a Person
 * David Leathers
 *
 * Implementation notes:
 *
 *  - Blink timing is kernel-driven via setitimer(ITIMER_REAL) in the child,
 *    not sleep loops. Each half-phase arms a one-shot timer and the child
 *    sigsuspends until SIGALRM (tick) or SIGUSR1 (preempt) fires.
 *
 *  - The classic sleep/signal race ("signal arrives between the while-check
 *    and pause()") is closed by blocking SIGALRM/SIGUSR1 outside the wait
 *    region and using sigsuspend() with an empty mask, which atomically
 *    unblocks and suspends.
 *
 *  - Parent blocks SIGUSR1 and SIGALRM BEFORE fork() so a just-forked child
 *    cannot be killed by default SIGUSR1 disposition in the window between
 *    fork and sigaction. Child inherits the block, installs handlers, and
 *    unblocks atomically via sigsuspend.
 *
 *  - Preempt ordering: parent sends SIGUSR1, BLOCKS in waitpid(child, 0),
 *    and only then fork()s the new child. This serializes GPIO writes --
 *    there is never a moment where two children race the same line.
 *
 *  - PR_SET_PDEATHSIG(SIGUSR1): if the server parent dies unexpectedly, the
 *    kernel delivers SIGUSR1 to the blink child, which drives LOW and exits
 *    cleanly instead of orphaning the libgpiod line (EBUSY on next open).
 *
 *  - libgpiod chip/line handles are heap-allocated in the parent and
 *    inherited across fork(); writes from the child use the inherited
 *    fd. The child does NOT release the line -- only the parent does, in
 *    led_destroy(). The child _exit()s and the kernel drops its fd refs.
 *
 *  - Build with -DLED_STUB on any non-Pi host to get a printf backend.
 *
 *  - TODO(Pi 5): this opens "gpiochip0" by name. Pi 5 exposes the 40-pin
 *    header as "gpiochip4" (gpiochip0 is internal). For Pi 3/4 in the lab
 *    this is correct; port to gpiod_chip_open_by_label("pinctrl-bcm2835")
 *    if a Pi 5 enters the picture.
 */

#include "led.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#ifndef LED_STUB
#include <gpiod.h>
#endif

struct Led
{
    int bcm_pin;
    pid_t child;              /* -1 if idle */
    uint8_t running_priority; /* valid only when child > 0 */
#ifndef LED_STUB
    struct gpiod_chip *chip;
    struct gpiod_line_request *line_request;
#endif
};

/* Child-process-only signal state. Lives in the child's address space
 * after fork(); the parent never touches these. */
static volatile sig_atomic_t g_tick = 0;
static volatile sig_atomic_t g_stop = 0;

static void on_sigalrm(int sig)
{
    (void)sig;
    g_tick = 1;
}
static void on_sigusr1(int sig)
{
    (void)sig;
    g_stop = 1;
}

/* ------------------------------------------------------------------ */
/* GPIO backend                                                        */
/* ------------------------------------------------------------------ */

#ifdef LED_STUB
static int gpio_open_(Led *led)
{
    (void)led;
    return 0;
}
static void gpio_close_(Led *led) { (void)led; }
static void gpio_write_(Led *led, int val)
{
    fprintf(stderr, "[led stub pin %d] %s\n",
            led->bcm_pin, val ? "HIGH" : "LOW");
}
#else
static int gpio_open_(Led *led)
{
    led->chip = gpiod_chip_open_by_name("gpiochip0");
    if (!led->chip)
    {
        fprintf(stderr, "led: gpiod_chip_open_by_name(gpiochip0) failed: %s\n",
                strerror(errno));
        return -1;
    }
    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    if (!req_cfg)
    {
        fprintf(stderr, "led: gpiod_request_config_new() failed\n");
        gpiod_chip_close(led->chip);
        led->chip = NULL;
        return -1;
    }
    gpiod_request_config_set_consumer(req_cfg, "lightserver-led");
    gpiod_request_config_add_line_by_offset(req_cfg, led->bcm_pin);
    gpiod_request_config_set_output_value(req_cfg, led->bcm_pin, 0);
    led->line_request = gpiod_chip_request_lines(led->chip, req_cfg);
    gpiod_request_config_free(req_cfg);
    if (!led->line_request)
    {
        fprintf(stderr, "led: gpiod_chip_request_lines(%d) failed: %s\n",
                led->bcm_pin, strerror(errno));
        gpiod_chip_close(led->chip);
        led->chip = NULL;
        return -1;
    }
    return 0;
}
static void gpio_close_(Led *led)
{
    if (led->line_request)
    {
        gpiod_line_request_release(led->line_request);
        led->line_request = NULL;
    }
    if (led->chip)
    {
        gpiod_chip_close(led->chip);
        led->chip = NULL;
    }
}
static void gpio_write_(Led *led, int val)
{
    if (led->line_request)
        gpiod_line_request_set_value(led->line_request, led->bcm_pin, val ? 1 : 0);
}
#endif

/* ------------------------------------------------------------------ */
/* Child-side blink loop                                               */
/* ------------------------------------------------------------------ */

static void arm_timer_ms_(unsigned ms)
{
    struct itimerval it;
    it.it_value.tv_sec = ms / 1000;
    it.it_value.tv_usec = (long)(ms % 1000) * 1000L;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0; /* one-shot */
    setitimer(ITIMER_REAL, &it, NULL);
}

static void child_body_(Led *led,
                        uint16_t count,
                        uint16_t on_ms,
                        uint16_t off_ms)
{
    /* Parent blocked SIGALRM + SIGUSR1 before fork(); we inherit that mask.
     * Re-block defensively in case we are ever called from a context that
     * did not pre-block. Idempotent. */
    sigset_t block, waitmask;
    sigemptyset(&block);
    sigaddset(&block, SIGALRM);
    sigaddset(&block, SIGUSR1);
    sigprocmask(SIG_BLOCK, &block, NULL);
    sigemptyset(&waitmask); /* during sigsuspend, unblock everything */

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_sigalrm;
    sigaction(SIGALRM, &sa, NULL);
    sa.sa_handler = on_sigusr1;
    sigaction(SIGUSR1, &sa, NULL);

#ifdef __linux__
    /* Ask kernel to send SIGUSR1 if our parent dies. Same signal as the
     * explicit preempt, so the handler above drives us to a clean shutdown
     * via the normal loop-exit path. */
    prctl(PR_SET_PDEATHSIG, SIGUSR1);
    /* Close the small race where parent died between fork and prctl: if
     * reparented to init, getppid() returns 1 (or a subreaper pid). */
    if (getppid() == 1)
    {
        gpio_write_(led, 0);
        _exit(0);
    }
#endif

    for (uint16_t i = 0; i < count && !g_stop; i++)
    {
        gpio_write_(led, 1);
        arm_timer_ms_(on_ms);
        while (!g_tick && !g_stop)
            sigsuspend(&waitmask);
        g_tick = 0;
        if (g_stop)
            break;

        gpio_write_(led, 0);
        arm_timer_ms_(off_ms);
        while (!g_tick && !g_stop)
            sigsuspend(&waitmask);
        g_tick = 0;
    }

    /* Always leave the line LOW before exit, whether we finished or
     * were preempted. */
    gpio_write_(led, 0);
    _exit(0);
}

/* ------------------------------------------------------------------ */
/* Parent API                                                          */
/* ------------------------------------------------------------------ */

Led *led_create(int bcm_pin)
{
    Led *led = (Led *)calloc(1, sizeof(Led));
    if (!led)
        return NULL;
    led->bcm_pin = bcm_pin;
    led->child = -1;
    led->running_priority = 0;
    if (gpio_open_(led) < 0)
    {
        free(led);
        return NULL;
    }
    gpio_write_(led, 0);
    return led;
}

void led_reap(Led *led)
{
    if (!led || led->child <= 0)
        return;
    int status;
    pid_t r = waitpid(led->child, &status, WNOHANG);
    if (r == led->child || r < 0)
    {
        /* reaped, or child was already gone */
        led->child = -1;
        led->running_priority = 0;
    }
    /* r == 0 means still running -- leave state as-is */
}

int led_busy(Led *led)
{
    if (!led)
        return 0;
    led_reap(led);
    return led->child > 0 ? 1 : 0;
}

void led_cancel(Led *led)
{
    if (!led || led->child <= 0)
        return;
    kill(led->child, SIGUSR1);
    int status;
    while (waitpid(led->child, &status, 0) < 0 && errno == EINTR)
    {
    }
    led->child = -1;
    led->running_priority = 0;
    gpio_write_(led, 0);
}

int led_blink(Led *led,
              uint16_t count,
              uint16_t on_ms,
              uint16_t off_ms,
              uint8_t priority)
{
    if (!led)
    {
        errno = EINVAL;
        return -1;
    }
    if (count == 0)
    {
        errno = EINVAL;
        return -1;
    }
    if (on_ms == 0)
        on_ms = 1;
    if (off_ms == 0)
        off_ms = 1;

    led_reap(led); /* fresh busy state */

    int preempted = 0;
    if (led->child > 0)
    {
        if (priority <= led->running_priority)
        {
            return 2; /* threshold-gate: reject */
        }
        /* Strictly higher priority -- preempt and serialize. */
        kill(led->child, SIGUSR1);
        int status;
        while (waitpid(led->child, &status, 0) < 0 && errno == EINTR)
        {
        }
        led->child = -1;
        led->running_priority = 0;
        gpio_write_(led, 0); /* defensive; child already drove LOW */
        preempted = 1;
    }

    /* Block SIGUSR1 + SIGALRM before fork so the child cannot be killed
     * by default SIGUSR1 disposition before installing its handler. */
    sigset_t fork_block, saved_mask;
    sigemptyset(&fork_block);
    sigaddset(&fork_block, SIGUSR1);
    sigaddset(&fork_block, SIGALRM);
    if (sigprocmask(SIG_BLOCK, &fork_block, &saved_mask) < 0)
    {
        perror("led_blink: sigprocmask");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        int saved_errno = errno;
        sigprocmask(SIG_SETMASK, &saved_mask, NULL);
        errno = saved_errno;
        perror("led_blink: fork");
        return -1;
    }
    if (pid == 0)
    {
        /* Child. Never returns. Inherits the block, installs handlers,
         * unblocks via sigsuspend. */
        child_body_(led, count, on_ms, off_ms);
    }

    /* Parent: restore the mask we had before the fork dance. */
    sigprocmask(SIG_SETMASK, &saved_mask, NULL);

    led->child = pid;
    led->running_priority = priority;
    return preempted ? 1 : 0;
}

void led_destroy(Led *led)
{
    if (!led)
        return;
    if (led->child > 0)
    {
        led_cancel(led);
    }
    else
    {
        gpio_write_(led, 0);
    }
    gpio_close_(led);
    free(led);
}
