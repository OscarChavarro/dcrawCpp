#include "../common/Options.h"
#include "../common/globals.h"
#include "../postprocessors/gamma.h"
#include "BayessianImage.h"

// Image model
BayessianImage THE_image;
unsigned IMAGE_colors;
unsigned IMAGE_filters;
unsigned short IMAGE_shrink;
unsigned short IMAGE_iheight;
unsigned short IMAGE_iwidth;

BayessianImage::BayessianImage() {
    bitsPerSample = 0;
    rawData = nullptr;
    height = 0;
    width = 0;
}

BayessianImage::~BayessianImage() {
}

void
adobe_copy_pixel(unsigned row, unsigned col, unsigned short **rp) {
    int c;

    if ( tiff_samples == 2 && OPTIONS_values->shotSelect ) {
        (*rp)++;
    }
    if ( THE_image.rawData ) {
        if ( row < THE_image.height && col < THE_image.width ) {
            RAW(row, col) = GAMMA_curveFunctionLookupTable[**rp];
        }
        *rp += tiff_samples;
    } else {
        if ( row < THE_image.height && col < THE_image.width ) {
            for ( c = 0; c < tiff_samples; c++ ) {
                GLOBAL_image[row * THE_image.width + col][c] = GAMMA_curveFunctionLookupTable[(*rp)[c]];
            }
        }
        *rp += tiff_samples;
    }
    if ( tiff_samples == 2 && OPTIONS_values->shotSelect ) {
        (*rp)--;
    }
}
