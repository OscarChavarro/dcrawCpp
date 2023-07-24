#include "BayessianImage.h"

// Image model
BayessianImage THE_image;

BayessianImage::BayessianImage() {
    bitsPerSample = 0;
    rawData = nullptr;
    height = 0;
    width = 0;
}

BayessianImage::~BayessianImage() {
}
