#ifndef __DCRAW_PIXELRGB16BITS__
#define __DCRAW_PIXELRGB16BITS__

#include <cmath>

class ColorRgb;

class PixelRgb16Bits {
  public:
    unsigned short r;
    unsigned short g;
    unsigned short b;

    PixelRgb16Bits();
    PixelRgb16Bits(unsigned short r, unsigned short g, unsigned short b);
    PixelRgb16Bits(ColorRgb *c, unsigned short bitsPerSample);
    ~PixelRgb16Bits();

    void ofColorRgb(ColorRgb *c, unsigned short bitsPerSample);
};

#include "ColorRgb.h"

#endif
