#ifndef __COLORRGB__
#define __COLORRGB__

class PixelRgb16Bits;

class ColorRgb {
  public:
    double r;
    double g;
    double b;

    ColorRgb();
    ColorRgb(double r, double g, double b);
    ColorRgb(PixelRgb16Bits *pixel, unsigned bitsPerSample);
    ~ColorRgb();
    void ofPixelRgb16Bits(PixelRgb16Bits *pixel, unsigned bitsPerSample);

    void print();
};

#include "PixelRgb16Bits.h"

#endif
