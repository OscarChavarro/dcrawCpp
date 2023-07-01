#include "../common/util.h"
#include "PixelRgb16Bits.h"

PixelRgb16Bits::PixelRgb16Bits() {
    r = g = b = 0;
}

PixelRgb16Bits::PixelRgb16Bits(unsigned short r, unsigned short g, unsigned short b) {
    this->r = r;
    this->g = g;
    this->b = b;
}

PixelRgb16Bits::PixelRgb16Bits(ColorRgb *c, unsigned short bitsPerSample) {
    ofColorRgb(c, bitsPerSample);
}

PixelRgb16Bits::~PixelRgb16Bits() {
}

void
PixelRgb16Bits::ofColorRgb(ColorRgb *c, unsigned short bitsPerSample) {
    unsigned short maxValue = (1 << bitsPerSample) - 1;
    r = clampRange((unsigned  short )floor(c->r * maxValue), 0, maxValue);
    g = clampRange((unsigned  short )floor(c->g * maxValue), 0, maxValue);
    b = clampRange((unsigned  short )floor(c->b * maxValue), 0, maxValue);
}
