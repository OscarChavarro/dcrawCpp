#ifndef __UTIL__
#define __UTIL__

extern void memoryError(void *ptr, const char *where);
extern void pseudoinverse(double (*in)[3], double (*out)[3], int size);
extern double clampUnitInterval(double x);
extern unsigned short clampRange(unsigned short value, unsigned short lowerLimit, unsigned short upperLimit);
extern unsigned short * make_decoder(const unsigned char *source);
extern unsigned short * make_decoder_ref(const unsigned char **source);
extern void crw_init_tables(unsigned table, unsigned short *huff[2]);

#endif
