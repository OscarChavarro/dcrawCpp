#ifndef __GLOBALS_IO__
#define __GLOBALS_IO__

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
extern unsigned short unsignedShortEndianSwap(const unsigned char *s);
extern unsigned unsignedEndianSwap(const unsigned char *s);
extern unsigned short read2bytes();
extern unsigned read4bytes();
extern unsigned readInt(int type);
extern double readDouble(int type);
extern void readShorts(unsigned short *pixel, int count);
extern float intToFloat(int i);

#endif
