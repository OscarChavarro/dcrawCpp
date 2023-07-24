#include <cstdio>
#include <cmath>
#include <cstring>
#include "CameraImageInformation.h"

CameraImageInformation CAMERA_IMAGE_information;

CameraImageInformation::CameraImageInformation() {
    resetValues();
}

CameraImageInformation::~CameraImageInformation() {
}

void
CameraImageInformation::resetValues() {
    inputFilename = nullptr;
}
