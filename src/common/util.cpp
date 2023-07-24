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

/*
Construct decode tree according the specification in *source.
The first 16 bytes specify how many codes should be 1-bit, 2-bit
3-bit, etc.  Bytes after that are the leaf values.

For example, if the source is

{ 0,1,4,2,3,1,2,0,0,0,0,0,0,0,0,0,
  0x04,0x03,0x05,0x06,0x02,0x07,0x01,0x08,0x09,0x00,0x0a,0x0b,0xff  },

then the code is

00		0x04
010		0x03
011		0x05
100		0x06
101		0x02
1100		0x07
1101		0x01
11100		0x08
11101		0x09
11110		0x00
111110		0x0a
1111110		0x0b
1111111		0xff
 */
unsigned short *
make_decoder_ref(const unsigned char **source) {
    int max;
    int len;
    int h;
    int i;
    int j;
    const unsigned char *count;
    unsigned short *huff;

    count = (*source += 16) - 17;
    for ( max = 16; max && !count[max]; max-- );
    huff = (unsigned short *)calloc(1 + (1 << max), sizeof *huff);
    memoryError(huff, "make_decoder()");
    huff[0] = max;
    for ( h = len = 1; len <= max; len++ ) {
        for ( i = 0; i < count[len]; i++, ++*source ) {
            for ( j = 0; j < 1 << (max - len); j++ ) {
                if ( h <= 1 << max ) {
                    huff[h++] = len << 8 | **source;
                }
            }
        }
    }
    return huff;
}

unsigned short *
make_decoder(const unsigned char *source) {
    return make_decoder_ref(&source);
}
