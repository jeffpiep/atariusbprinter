/*
 * POSIX sleep stubs for RP2040 bare-metal.
 *
 * libstdc++.a(thread.o) is pulled in by std::function's internals and
 * defines std::this_thread::__sleep_for(), which references usleep() and
 * sleep(). These POSIX functions are not available in the arm-none-eabi
 * newlib build, so we provide forwarding stubs using the pico SDK.
 *
 * std::this_thread::sleep_for() is never actually called at runtime.
 */
#include "pico/time.h"

int usleep(unsigned int usec) {
    sleep_us(usec);
    return 0;
}

unsigned int sleep(unsigned int seconds) {
    sleep_ms(seconds * 1000u);
    return 0;
}
