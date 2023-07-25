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
