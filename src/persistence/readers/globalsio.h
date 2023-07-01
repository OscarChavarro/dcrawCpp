#ifndef __GLOBALSIO__
#define __GLOBALSIO__

#ifdef LINUX_PLATFORM
#include <arpa/inet.h>
    #ifndef _GNU_SOURCE
        // swab function
        #define _GNU_SOURCE
    #endif
#endif

#include <cstdio>

#endif
