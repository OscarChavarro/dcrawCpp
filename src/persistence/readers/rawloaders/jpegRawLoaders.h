#ifndef __JPEG_RAW_LOADERS__
#define __JPEG_RAW_LOADERS__

struct jhead {
    int algo;
    int bits;
    int high;
    int wide;
    int clrs;
    int sraw;
    int psv;
    int restart;
    int vpred[6];
    unsigned short quant[64];
    unsigned short idct[64];
    unsigned short *huff[20];
    unsigned short *free[20];
    unsigned short *row;
};

unsigned short *ljpeg_row(int jrow, struct jhead *jh);
extern int ljpeg_diff(unsigned short *huff);

#endif
