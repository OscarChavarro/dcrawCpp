#include <cstdio>
#include <cstdlib>

#include "CameraImageInformation.h"
#include "util.h"

void
memoryError(void *ptr, const char *where) {
    if ( ptr ) {
        return;
    }
    fprintf(stderr, "%s: Out of memory in %s\n", CAMERA_IMAGE_information.inputFilename, where);
    exit(1);
}

void
pseudoinverse(double (*in)[3], double (*out)[3], int size) {
    double work[3][6];
    double num;
    int i;
    int j;
    int k;

    for ( i = 0; i < 3; i++ ) {
        for ( j = 0; j < 6; j++ ) {
            work[i][j] = j == i + 3;
        }
        for ( j = 0; j < 3; j++ ) {
            for ( k = 0; k < size; k++ ) {
                work[i][j] += in[k][i] * in[k][j];
            }
        }
    }

    for ( i = 0; i < 3; i++ ) {
        num = work[i][i];
        for ( j = 0; j < 6; j++ ) {
            work[i][j] /= num;
        }
        for ( k = 0; k < 3; k++ ) {
            if ( k == i ) {
                continue;
            }
            num = work[k][i];
            for ( j = 0; j < 6; j++ ) {
                work[k][j] -= work[i][j] * num;
            }
        }
    }
    for ( i = 0; i < size; i++ ) {
        for ( j = 0; j < 3; j++ ) {
            for ( out[i][j] = k = 0; k < 3; k++ ) {
                out[i][j] += work[j][k + 3] * in[i][k];
            }
        }
    }
}

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
