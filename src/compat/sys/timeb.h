/*
 * Stub sys/timeb.h for Cosmopolitan libc compatibility.
 * WAMR's linux/platform_internal.h includes this header but never
 * actually uses ftime(). This stub satisfies the #include directive.
 */
#ifndef _SYS_TIMEB_H
#define _SYS_TIMEB_H

#include <sys/types.h>

struct timeb {
    time_t time;
    unsigned short millitm;
    short timezone;
    short dstflag;
};

#endif /* _SYS_TIMEB_H */
