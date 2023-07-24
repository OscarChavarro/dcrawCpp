#include "../../../common/globals.h"
#include "../../../common/util.h"
#include "../../../colorRepresentation/adobeCoeff.h"
#include "../../../postprocessors/gamma.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../globalsio.h"
#include "jpegRawLoaders.h"
#include "sonyRawLoaders.h"

void
sony_decrypt(unsigned *data, int len, int start, int key) {
    static unsigned pad[128];
    static unsigned p;

    if ( start ) {
        for ( p = 0; p < 4; p++ ) {
            pad[p] = key = key * 48828125 + 1;
        }
        pad[3] = pad[3] << 1 | (pad[0] ^ pad[2]) >> 31;
        for ( p = 4; p < 127; p++ ) {
            pad[p] = (pad[p - 4] ^ pad[p - 2]) << 1 | (pad[p - 3] ^ pad[p - 1]) >> 31;
        }
        for ( p = 0; p < 127; p++ ) {
            pad[p] = htonl(pad[p]);
        }
    }
    while ( len-- && p++ ) {
        *data++ ^= pad[(p - 1) & 127] = pad[p & 127] ^ pad[(p + 64) & 127];
    }
}

void
sony_load_raw() {
    unsigned char head[40];
    unsigned short *pixel;
    unsigned i;
    unsigned key;
    unsigned row;
    unsigned col;

    fseek(GLOBAL_IO_ifp, 200896, SEEK_SET);
    fseek(GLOBAL_IO_ifp, (unsigned)fgetc(GLOBAL_IO_ifp) * 4 - 1, SEEK_CUR);
    GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
    key = read4bytes();
    fseek(GLOBAL_IO_ifp, 164600, SEEK_SET);
    fread(head, 1, 40, GLOBAL_IO_ifp);
    sony_decrypt((unsigned *)head, 10, 1, key);
    for ( i = 26; i-- > 22; ) {
        key = key << 8 | head[i];
    }
    fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset, SEEK_SET);
    for ( row = 0; row < THE_image.height; row++ ) {
        pixel = THE_image.rawData + row * THE_image.width;
        if ( fread(pixel, 2, THE_image.width, GLOBAL_IO_ifp) < THE_image.width ) {
            inputOutputError();
        }
        sony_decrypt((unsigned *)pixel, THE_image.width / 2, !row, key);
        for ( col = 0; col < THE_image.width; col++ ) {
            if ( (pixel[col] = ntohs(pixel[col])) >> 14 ) {
                inputOutputError();
            }
        }
    }
    ADOBE_maximum = 0x3ff0;
}

void
sony_arw_load_raw() {
    unsigned short huff[32770];
    static const unsigned short tab[18] =
            {0xf11, 0xf10, 0xe0f, 0xd0e, 0xc0d, 0xb0c, 0xa0b, 0x90a, 0x809,
             0x708, 0x607, 0x506, 0x405, 0x304, 0x303, 0x300, 0x202, 0x201};
    int i;
    int c;
    int n;
    int col;
    int row;
    int sum = 0;

    huff[0] = 15;
    for ( n = i = 0; i < 18; i++ ) {
        for ( c = 0; c < 32768 >> (tab[i] >> 8); c++ ) {
            huff[++n] = tab[i];
        }
    }
    getbits(-1);
    for ( col = THE_image.width; col--; ) {
        for ( row = 0; row < THE_image.height + 1; row += 2 ) {
            if ( row == THE_image.height ) {
                row = 1;
            }
            if ( (sum += ljpeg_diff(huff)) >> 12 ) {
                inputOutputError();
            }
            if ( row < THE_image.height ) {
                RAW(row, col) = sum;
            }
        }
    }
}

/**
Used by SONY A7RIV camera on 12 bits loosely compressed RAW files.
*/
void
sony_arw2_load_raw() {
    unsigned char *data;
    unsigned char *dp;
    unsigned short pix[16];
    int row;
    int col;
    int val;
    int max;
    int min;
    int imax;
    int imin;
    int sh;
    int bit;
    int i;

    data = (unsigned char *)malloc(THE_image.width + 1);
    memoryError(data, "sony_arw2_load_raw()");
    for ( row = 0; row < THE_image.height; row++ ) {
        fread(data, 1, THE_image.width, GLOBAL_IO_ifp);
        for ( dp = data, col = 0; col < THE_image.width - 30; dp += 16 ) {
            max = 0x7ff & (val = unsignedEndianSwap(dp));
            min = 0x7ff & val >> 11;
            imax = 0x0f & val >> 22;
            imin = 0x0f & val >> 26;
            for ( sh = 0; sh < 4 && 0x80 << sh <= max - min; sh++ );
            for ( bit = 30, i = 0; i < 16; i++ ) {
                if ( i == imax ) {
                    pix[i] = max;
                } else {
                    if ( i == imin ) {
                        pix[i] = min;
                    } else {
                        pix[i] = ((unsignedShortEndianSwap(dp + (bit >> 3)) >> (bit & 7) & 0x7f) << sh) + min;
                        if ( pix[i] > 0x7ff ) {
                            pix[i] = 0x7ff;
                        }
                        bit += 7;
                    }
                }
            }

            // Gamma correction at loading time to scale to same 14-bit data range
            for ( i = 0; i < 16; i++, col += 2 ) {
                RAW(row, col) = GAMMA_curveFunctionLookupTable[pix[i] << 1] >> 2;
            }
            col -= col & 1 ? 1 : 31;
        }
    }
    free(data);
}
