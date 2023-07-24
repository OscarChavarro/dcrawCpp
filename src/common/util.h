#ifndef __UTIL__
#define __UTIL__

extern void memoryError(void *ptr, const char *where);
extern void pseudoinverse(double (*in)[3], double (*out)[3], int size);
extern double clampUnitInterval(double x);
extern unsigned short clampRange(unsigned short value, unsigned short lowerLimit, unsigned short upperLimit);

#endif
