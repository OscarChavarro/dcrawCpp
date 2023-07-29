#include <cstring>
#include "../../../common/globals.h"
#include "../../../common/mathMacros.h"
#include "../../../common/util.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../../../colorRepresentation/adobeCoeff.h"
#include "../globalsio.h"
#include "nokiaRawLoaders.h"

void
nokia_load_raw() {
    unsigned char *data;
    unsigned char *dp;
    int rev;
    int dwide;
    int row;
    int col;
    int c;
    double sum[] = {0, 0};

    rev = 3 * (GLOBAL_endianOrder == LITTLE_ENDIAN_ORDER);
    dwide = (THE_image.width * 5 + 1) / 4;
    data = (unsigned char *) malloc(dwide * 2);
    memoryError(data, "nokia_load_raw()");
    for ( row = 0; row < THE_image.height; row++ ) {
        if ( fread(data + dwide, 1, dwide, GLOBAL_IO_ifp) < dwide ) inputOutputError();
        for ( c = 0; c < dwide; c++ ) {
            data[c] = data[dwide + (c ^ rev)];
        }
        for ( dp = data, col = 0; col < THE_image.width; dp += 5, col += 4 ) {
            for ( c = 0; c < 4; c++ ) {
                RAW(row, col + c) = (dp[c] << 2) | (dp[4] >> (c << 1) & 3);
            }
        }
    }
    free(data);
    ADOBE_maximum = 0x3ff;
    if ( strcmp(GLOBAL_make, "OmniVision") != 0 ) {
        return;
    }
    row = THE_image.height / 2;
    for ( c = 0; c < width - 1; c++ ) {
        sum[c & 1] += SQR(RAW(row, c) - RAW(row + 1, c + 1));
        sum[~c & 1] += SQR(RAW(row + 1, c) - RAW(row, c + 1));
    }
    if ( sum[1] > sum[0] ) {
        IMAGE_filters = 0x4b4b4b4b;
    }
}
