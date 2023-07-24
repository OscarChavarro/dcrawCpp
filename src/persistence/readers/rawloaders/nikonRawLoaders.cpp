#include <cstring>
#include "../../../common/globals.h"
#include "../globalsio.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../../../postprocessors/gamma.h"
#include "../../../common/mathMacros.h"
#include "../../../common/util.h"
#include "nikonRawLoaders.h"

void
nikon_load_raw() {
    static const unsigned char nikon_tree[][32] = {
            {0, 1, 5, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0,    /* 12-bit lossy */
                    5,    4,    3,    6,    2,    7, 1,  0, 8,  9,  11, 10, 12},
            {0, 1, 5, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0,    /* 12-bit lossy after split */
                    0x39, 0x5a, 0x38, 0x27, 0x16, 5, 4,  3, 2,  1,  0,  11, 12, 12},
            {0, 1, 4, 2, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 12-bit lossless */
                    5,    4,    6,    3,    7,    2, 8,  1, 9,  0,  10, 11, 12},
            {0, 1, 4, 3, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0,    /* 14-bit lossy */
                    5,    6,    4,    7,    8,    3, 9,  2, 1,  0,  10, 11, 12, 13, 14},
            {0, 1, 5, 1, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0,    /* 14-bit lossy after split */
                    8,    0x5c, 0x4b, 0x3a, 0x29, 7, 6,  5, 4,  3,  2,  1,  0,  13, 14},
            {0, 1, 4, 2, 2, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0,    /* 14-bit lossless */
                    7,    6,    8,    5,    9,    4, 10, 3, 11, 12, 2,  0,  1,  13, 14}};
    unsigned short *huff;
    unsigned short ver0;
    unsigned short ver1;
    unsigned short vpred[2][2];
    unsigned short hpred[2];
    unsigned short csize;
    int i;
    int min;
    int max;
    int step = 0;
    int tree = 0;
    int split = 0;
    int row;
    int col;
    int len;
    int shl;
    int diff;

    fseek(GLOBAL_IO_ifp, GLOBAL_meta_offset, SEEK_SET);
    ver0 = fgetc(GLOBAL_IO_ifp);
    ver1 = fgetc(GLOBAL_IO_ifp);
    if ( ver0 == 0x49 || ver1 == 0x58 ) {
        fseek(GLOBAL_IO_ifp, 2110, SEEK_CUR);
    }
    if ( ver0 == 0x46 ) {
        tree = 2;
    }
    if ( THE_image.bitsPerSample == 14 ) {
        tree += 3;
    }
    readShorts(vpred[0], 4);
    max = 1 << THE_image.bitsPerSample & 0x7fff;
    if ((csize = read2bytes()) > 1 ) {
        step = max / (csize - 1);
    }
    if ( ver0 == 0x44 && ver1 == 0x20 && step > 0 ) {
        for ( i = 0; i < csize; i++ ) {
            GAMMA_curveFunctionLookupTable[i * step] = read2bytes();
        }
        for ( i = 0; i < max; i++ ) {
            GAMMA_curveFunctionLookupTable[i] = (GAMMA_curveFunctionLookupTable[i - i % step] * (step - i % step) +
                                                 GAMMA_curveFunctionLookupTable[i - i % step + step] * (i % step)) / step;
        }
        fseek(GLOBAL_IO_ifp, GLOBAL_meta_offset + 562, SEEK_SET);
        split = read2bytes();
    } else {
        if ( ver0 != 0x46 && csize <= 0x4001 ) {
            readShorts(GAMMA_curveFunctionLookupTable, max = csize);
        }
    }
    while ( GAMMA_curveFunctionLookupTable[max - 2] == GAMMA_curveFunctionLookupTable[max - 1] ) {
        max--;
    }
    huff = make_decoder(nikon_tree[tree]);
    fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset, SEEK_SET);
    getbits(-1);
    for ( min = row = 0; row < THE_image.height; row++ ) {
        if ( split && row == split ) {
            free(huff);
            huff = make_decoder(nikon_tree[tree + 1]);
            max += (min = 16) << 1;
        }
        for ( col = 0; col < THE_image.width; col++ ) {
            i = gethuff(huff);
            len = i & 15;
            shl = i >> 4;
            diff = ((getbits(len - shl) << 1) + 1) << shl >> 1;
            if ((diff & (1 << (len - 1))) == 0 ) {
                diff -= (1 << len) - !shl;
            }
            if ( col < 2 ) {
                hpred[col] = vpred[row & 1][col] += diff;
            } else {
                hpred[col & 1] += diff;
            }
            if ( (unsigned short) (hpred[col & 1] + min) >= max ) {
                inputOutputError();
            }
            RAW(row, col) = GAMMA_curveFunctionLookupTable[LIM((short)hpred[col & 1], 0, 0x3fff)];
        }
    }
    free(huff);
}

void
nikon_yuv_load_raw() {
    int row;
    int col;
    int yuv[4];
    int rgb[3];
    int b;
    int c;
    unsigned long long bitbuf = 0;

    for ( row = 0; row < THE_image.height; row++ ) {
        for ( col = 0; col < THE_image.width; col++ ) {
            if ( !(b = col & 1)) {
                bitbuf = 0;
                for ( c = 0; c < 6; c++ ) {
                    bitbuf |= (unsigned long long) fgetc(GLOBAL_IO_ifp) << c * 8;
                }
                for ( c = 0; c < 4; c++ ) {
                    yuv[c] = (bitbuf >> c * 12 & 0xfff) - (c >> 1 << 11);
                }
            }
            rgb[0] = yuv[b] + 1.370705 * yuv[3];
            rgb[1] = yuv[b] - 0.337633 * yuv[2] - 0.698001 * yuv[3];
            rgb[2] = yuv[b] + 1.732446 * yuv[2];
            for ( c = 0; c < 3; c++ ) {
                GLOBAL_image[row * THE_image.width + col][c] = GAMMA_curveFunctionLookupTable[LIM(rgb[c], 0, 0xfff)] / GLOBAL_cam_mul[c];
            }
        }
    }
}

/*
Returns 1 for a Coolpix 995, 0 for anything else.
*/
int
nikon_e995() {
    int i;
    int histo[256];
    const unsigned char often[] = {0x00, 0x55, 0xaa, 0xff};

    memset(histo, 0, sizeof histo);
    fseek(GLOBAL_IO_ifp, -2000, SEEK_END);
    for ( i = 0; i < 2000; i++ ) {
        histo[fgetc(GLOBAL_IO_ifp)]++;
    }
    for ( i = 0; i < 4; i++ ) {
        if ( histo[often[i]] < 200 ) {
            return 0;
        }
    }
    return 1;
}

/*
Returns 1 for a Coolpix 2100, 0 for anything else.
*/
int
nikon_e2100() {
    unsigned char t[12];
    int i;

    fseek(GLOBAL_IO_ifp, 0, SEEK_SET);
    for ( i = 0; i < 1024; i++ ) {
        fread(t, 1, 12, GLOBAL_IO_ifp);
        if ( ((t[2] & t[4] & t[7] & t[9]) >> 4
              & t[1] & t[6] & t[8] & t[11] & 3) != 3 ) {
            return 0;
        }
    }
    return 1;
}

void
nikon_3700() {
    int bits;
    int i;
    unsigned char dp[24];
    static const struct {
        int bits;
        char make[12];
        char model[15];
    } table[] = {
            {0x00, "Pentax",  "Optio 33WR"},
            {0x03, "Nikon",   "E3200"},
            {0x32, "Nikon",   "E3700"},
            {0x33, "Olympus", "C740UZ"}};

    fseek(GLOBAL_IO_ifp, 3072, SEEK_SET);
    fread(dp, 1, 24, GLOBAL_IO_ifp);
    bits = (dp[8] & 3) << 4 | (dp[20] & 3);
    for ( i = 0; i < sizeof table / sizeof *table; i++ )
        if ( bits == table[i].bits ) {
            strcpy(GLOBAL_make, table[i].make);
            strcpy(GLOBAL_model, table[i].model);
        }
}
