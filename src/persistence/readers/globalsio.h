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

extern FILE *GLOBAL_IO_ifp;
extern unsigned GLOBAL_IO_dataError;

extern void inputOutputError();
extern unsigned short sget2(const unsigned char *s);
extern unsigned unsignedShortEndianSwap(unsigned char *s);
extern unsigned short get2();
extern unsigned get4();
extern unsigned getInt(int type);
extern double getReal(int type);
extern void readShorts(unsigned short *pixel, int count);
extern float intToFloat(int i);

#endif
