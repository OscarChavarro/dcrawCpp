#ifndef __GLOBALS_IMAGE_MODEL__
#define __GLOBALS_IMAGE_MODEL__

#include "../colorRepresentation/PixelRgb16Bits.h"
#include "../colorRepresentation/ColorRgb.h"

class BayessianImage {
  public:
    /**
     * rawData contains a 2D matrix of positive integer values as comming raw from camera Bayesian filter RGGB mosaic.
     * Each value is the [0, 2 ^ bitsPerSample - 1] range.
     */
    unsigned short *rawData;

    /**
     * Typical values of bitsPerSample:
     * 1: for binary ADOBE_black / white images
     * 8: for gray tone (monocrom mode) or 24 bpp (rbg mode)
     * 12: SONY A7r4 compressed raw
     * 14: SONY A7r4 uncompressed raw
     */
    unsigned short bitsPerSample;

    unsigned short height;
    unsigned short width;

    BayessianImage();
    ~BayessianImage();
};

/*
In order to inline this calculation, I make the risky
assumption that all filter patterns can be described
by a repeating pattern of eight rows and two columns

Do not use the FC or BAYER macros with the Leaf CatchLight,
because its pattern is 16x16, not 2x8.

Return values are either 0/1/2/3 = G/M/C/Y or 0/1/2/3 = R/G1/B/G2

PowerShot 600	PowerShot A50	PowerShot Pro70	Pro90 & G1
0xe1e4e1e4:	0x1b4e4b1e:	0x1e4b4e1b:	0xb4b4b4b4:

  0 1 2 3 4 5	  0 1 2 3 4 5	  0 1 2 3 4 5	  0 1 2 3 4 5
0 G M G M G M	0 C Y C Y C Y	0 Y C Y C Y C	0 G M G M G M
1 C Y C Y C Y	1 M G M G M G	1 M G M G M G	1 Y C Y C Y C
2 M G M G M G	2 Y C Y C Y C	2 C Y C Y C Y
3 C Y C Y C Y	3 G M G M G M	3 G M G M G M
        4 C Y C Y C Y	4 Y C Y C Y C
PowerShot A5	5 G M G M G M	5 G M G M G M
0x1e4e1e4e:	6 Y C Y C Y C	6 C Y C Y C Y
        7 M G M G M G	7 M G M G M G
  0 1 2 3 4 5
0 C Y C Y C Y
1 G M G M G M
2 C Y C Y C Y
3 M G M G M G

All RGB cameras use one of these Bayer grids:

0x16161616:	0x61616161:	0x49494949:	0x94949494:

  0 1 2 3 4 5	  0 1 2 3 4 5	  0 1 2 3 4 5	  0 1 2 3 4 5
0 B G B G B G	0 G R G R G R	0 G B G B G B	0 R G R G R G
1 G R G R G R	1 B G B G B G	1 R G R G R G	1 G B G B G B
2 B G B G B G	2 G R G R G R	2 G B G B G B	2 R G R G R G
3 G R G R G R	3 B G B G B G	3 R G R G R G	3 G B G B G B
*/

#define RAW(row, col) \
    THE_image.rawData[(row)*THE_image.width+(col)]

#define FC(row, col) \
    (IMAGE_filters >> ((((row) << 1 & 14) + ((col) & 1)) << 1) & 3)

#define BAYER(row, col) \
    GLOBAL_image[((row) >> IMAGE_shrink)*IMAGE_iwidth + ((col) >> IMAGE_shrink)][FC(row,col)]

#define BAYER2(row, col) \
    GLOBAL_image[((row) >> IMAGE_shrink)*IMAGE_iwidth + ((col) >> IMAGE_shrink)][fcol(row,col)]

extern BayessianImage THE_image;
extern unsigned IMAGE_colors;
extern unsigned IMAGE_filters;
extern unsigned short IMAGE_shrink;
extern unsigned short IMAGE_iheight;
extern unsigned short IMAGE_iwidth;
extern void adobe_copy_pixel(unsigned row, unsigned col, unsigned short **rp);
extern int raw(unsigned row, unsigned col);

#endif
