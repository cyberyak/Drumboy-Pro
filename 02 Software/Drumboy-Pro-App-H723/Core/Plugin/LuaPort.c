#if defined(DRUMBOY_PLUGINS) && defined(DRUMBOY_LUA)

#include <sys/time.h>

#include "main.h"

/* Lua Port ---------------------------------------------------------*/
// newlib's time() (used by Lua to seed its hash and math.random) needs
// _gettimeofday, which the stock syscalls.c doesn't provide. Returns the
// time since boot; there is no real-time clock.

int _gettimeofday(struct timeval* tv, void* tz) {
    (void)tz;
    if (tv != NULL) {
        uint32_t ms = HAL_GetTick();
        tv->tv_sec = ms / 1000;
        tv->tv_usec = (ms % 1000) * 1000;
    }
    return 0;
}

#endif
