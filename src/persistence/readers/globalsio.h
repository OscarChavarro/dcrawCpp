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
extern FILE *ofp;
extern unsigned GLOBAL_IO_zeroAfterFf;
extern unsigned GLOBAL_IO_dataError;
extern off_t GLOBAL_IO_profileOffset;

#define getbits(n) getbithuff((n), 0)
#define gethuff(h) getbithuff(*(h), (h) + 1)
#define ph1_bits(n) ph1_bithuff(n,0)
#define ph1_huff(h) ph1_bithuff(*h,h+1)

extern void inputOutputError();
extern unsigned short unsignedShortEndianSwap(const unsigned char *s);
extern unsigned unsignedEndianSwap(const unsigned char *s);
extern unsigned short read2bytes();
extern unsigned read4bytes();
extern unsigned readInt(int type);
extern double readDouble(int type);
extern void readShorts(unsigned short *pixel, int count);
extern float intToFloat(int i);
extern unsigned getbithuff(int nbits, const unsigned short *huff);
extern unsigned ph1_bithuff(int nbits, unsigned short *huff);

#endif
