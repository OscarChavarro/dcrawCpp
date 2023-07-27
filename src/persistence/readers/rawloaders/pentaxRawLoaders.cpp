#include "../../../common/globals.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../globalsio.h"
#include "jpegRawLoaders.h"
#include "pentaxRawLoaders.h"

void
pentax_load_raw() {
    unsigned short bit[2][15];
    unsigned short huff[4097];
    int dep;
    int row;
    int col;
    int diff;
    int c;
    int i;
    unsigned short vpred[2][2] = {{0, 0},
                                  {0, 0}};
    unsigned short hpred[2];

    fseek(GLOBAL_IO_ifp, GLOBAL_meta_offset, SEEK_SET);
    dep = (read2bytes() + 12) & 15;
    fseek(GLOBAL_IO_ifp, 12, SEEK_CUR);
    for ( c = 0; c < dep; c++ ) {
        bit[0][c] = read2bytes();
    }
    for ( c = 0; c < dep; c++ ) {
        bit[1][c] = fgetc(GLOBAL_IO_ifp);
    }
    for ( c = 0; c < dep; c++ ) {
        for ( i = bit[0][c]; i <= ((bit[0][c] + (4096 >> bit[1][c]) - 1) & 4095); ) {
            huff[++i] = bit[1][c] << 8 | c;
        }
    }
    huff[0] = 12;
    fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset, SEEK_SET);
    getbits(-1);
    for ( row = 0; row < THE_image.height; row++ ) {
        for ( col = 0; col < THE_image.width; col++ ) {
            diff = ljpeg_diff(huff);
            if ( col < 2 ) {
                hpred[col] = vpred[row & 1][col] += diff;
            } else {
                hpred[col & 1] += diff;
            }
            RAW(row, col) = hpred[col & 1];
            if ( hpred[col & 1] >> THE_image.bitsPerSample ) {
                inputOutputError();
            }
        }
    }
}
