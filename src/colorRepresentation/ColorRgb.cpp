#include <cstdio>

#include "../common/util.h"
#include "ColorRgb.h"

ColorRgb::ColorRgb() {
    r = g = b = 0.0;
}

ColorRgb::ColorRgb(double r, double g, double b) {
    this->r = r;
    this->g = g;
    this->b = b;
}

ColorRgb::ColorRgb(PixelRgb16Bits *pixel, unsigned bitsPerSample) {
    ofPixelRgb16Bits(pixel, bitsPerSample);
}

ColorRgb::~ColorRgb() {
}

void
ColorRgb::ofPixelRgb16Bits(PixelRgb16Bits *pixel, unsigned bitsPerSample) {
    double maxValue = (double)(1 << bitsPerSample) - 1;
    r = clampUnitInterval(((double)pixel->r) / maxValue);
    g = clampUnitInterval(((double)pixel->g) / maxValue);
    b = clampUnitInterval(((double)pixel->b) / maxValue);
}

void
ColorRgb::print() {
    printf("<%0.2f, %0.2f, %0.2f>", r, g, b);
}