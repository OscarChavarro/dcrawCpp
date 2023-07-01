#include <cstdio>
#include <cstdlib>

#include "globals.h"
#include "util.h"
#include "options.h"
#include "CameraImageInformation.h"

double clampUnitInterval(double x) {
    if (x < 0.0) {
        return 0.0;
    } else if (x > 1.0) {
            return 1.0;
        } else {
            return x;
        }
}

unsigned short
clampRange(unsigned short value, unsigned short lowerLimit, unsigned short upperLimit) {
    if (value < lowerLimit) {
        return lowerLimit;
    } else if (value > upperLimit) {
            return upperLimit;
        } else {
            return value;
        }
}
