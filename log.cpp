#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <time.h>

#include "log.h"
#include "util_time.h"


void log_debug(const char *function, int line, int debug,const char *message,...)
{
    if(((debug) & DBG_ON)
            && ((debug) & DBG_TYPES_ON)
            && (((debug) & DBG_MASK_LEVEL) >= DBG_MIN_LEVEL))
    {
        char msg[256];
        char ti[32];
        va_list p;

        va_start(p, message);
        vsprintf(msg, message, p);
        va_end(p);

        time_t now = time(NULL);

        strftime(ti, sizeof(ti), "%Y-%m-%d %H:%M:%S", localtime(&now));
        printf("[%s %lld] [%s(%d)]  %s\n", ti, get_current_time_msec()%1000,
               function, line, msg);
    }
}
