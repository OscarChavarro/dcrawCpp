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

extern BayessianImage THE_image;

#endif
