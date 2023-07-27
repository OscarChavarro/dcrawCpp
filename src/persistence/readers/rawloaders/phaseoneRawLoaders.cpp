#include "../../../common/globals.h"
#include "../../../common/util.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../../../colorRepresentation/adobeCoeff.h"
#include "../../../postprocessors/gamma.h"
#include "../globalsio.h"
#include "phaseoneRawLoaders.h"

void
phase_one_load_raw() {
    int a;
    int b;
    int i;
    unsigned short akey, bkey, mask;

    fseek(GLOBAL_IO_ifp, ph1.key_off, SEEK_SET);
    akey = read2bytes();
    bkey = read2bytes();
    mask = ph1.format == 1 ? 0x5555 : 0x1354;
    fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset, SEEK_SET);
    readShorts(THE_image.rawData, THE_image.width * THE_image.height);
    if ( ph1.format ) {
        for ( i = 0; i < THE_image.width * THE_image.height; i += 2 ) {
            a = THE_image.rawData[i + 0] ^ akey;
            b = THE_image.rawData[i + 1] ^ bkey;
            THE_image.rawData[i + 0] = (a & mask) | (b & ~mask);
            THE_image.rawData[i + 1] = (b & mask) | (a & ~mask);
        }
    }
}

void
phase_one_load_raw_c() {
    static const int length[] = {8, 7, 6, 9, 11, 10, 5, 12, 14, 13};
    int *offset;
    int len[2];
    int pred[2];
    int row;
    int col;
    int i;
    int j;
    unsigned short *pixel;
    short (*cblack)[2];
    short (*rblack)[2];

    pixel = (unsigned short *) calloc(THE_image.width * 3 + THE_image.height * 4, 2);
    memoryError(pixel, "phase_one_load_raw_c()");
    offset = (int *) (pixel + THE_image.width);
    fseek(GLOBAL_IO_ifp, strip_offset, SEEK_SET);
    for ( row = 0; row < THE_image.height; row++ ) {
        offset[row] = read4bytes();
    }
    cblack = (short (*)[2]) (offset + THE_image.height);
    fseek(GLOBAL_IO_ifp, ph1.black_col, SEEK_SET);
    if ( ph1.black_col ) {
        readShorts((unsigned short *) cblack[0], THE_image.height * 2);
    }
    rblack = cblack + THE_image.height;
    fseek(GLOBAL_IO_ifp, ph1.black_row, SEEK_SET);
    if ( ph1.black_row ) {
        readShorts((unsigned short *) rblack[0], THE_image.width * 2);
    }
    for ( i = 0; i < 256; i++ ) {
        GAMMA_curveFunctionLookupTable[i] = i * i / 3.969 + 0.5;
    }
    for ( row = 0; row < THE_image.height; row++ ) {
        fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset + offset[row], SEEK_SET);
        ph1_bits(-1);
        pred[0] = pred[1] = 0;
        for ( col = 0; col < THE_image.width; col++ ) {
            if ( col >= (THE_image.width & -8) ) {
                len[0] = len[1] = 14;
            } else {
                if ((col & 7) == 0 ) {
                    for ( i = 0; i < 2; i++ ) {
                        for ( j = 0; j < 5 && !ph1_bits(1); j++ );
                        if ( j-- ) {
                            len[i] = length[j * 2 + ph1_bits(1)];
                        }
                    }
                }
            }
            if ((i = len[col & 1]) == 14 ) {
                pixel[col] = pred[col & 1] = ph1_bits(16);
            } else {
                pixel[col] = pred[col & 1] += ph1_bits(i) + 1 - (1 << (i - 1));
            }
            if ( pred[col & 1] >> 16 ) {
                inputOutputError();
            }
            if ( ph1.format == 5 && pixel[col] < 256 ) {
                pixel[col] = GAMMA_curveFunctionLookupTable[pixel[col]];
            }
        }
        for ( col = 0; col < THE_image.width; col++ ) {
            i = (pixel[col] << 2 * (ph1.format != 8)) - ph1.black
                + cblack[row][col >= ph1.split_col]
                + rblack[col][row >= ph1.split_row];
            if ( i > 0 ) {
                RAW(row, col) = i;
            }
        }
    }
    free(pixel);
    ADOBE_maximum = 0xfffc - ph1.black;
}
