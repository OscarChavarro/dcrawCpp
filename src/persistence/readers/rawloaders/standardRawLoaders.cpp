#include "../../../common/globals.h"
#include "../../../colorRepresentation/adobeCoeff.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../globalsio.h"
#include "standardRawLoaders.h"

void
unpacked_load_raw() {
    int row;
    int col;
    int bits = 0;

    while ( 1 << ++bits < ADOBE_maximum );
    readShorts(THE_image.rawData, THE_image.width * THE_image.height);
    for ( row = 0; row < THE_image.height; row++ ) {
        for ( col = 0; col < THE_image.width; col++ ) {
            if ((RAW(row, col) >>= GLOBAL_loadFlags) >> bits
                && (unsigned) (row - top_margin) < THE_image.height
                && (unsigned) (col - left_margin) < THE_image.width ) {
                inputOutputError();
            }
        }
    }
}

void
packed_load_raw() {
    int vbits = 0;
    int bwide;
    int rbits;
    int bite;
    int half;
    int irow;
    int row;
    int col;
    int val;
    int i;
    unsigned long long bitbuf = 0;

    bwide = THE_image.width * THE_image.bitsPerSample / 8;
    bwide += bwide & GLOBAL_loadFlags >> 9;
    rbits = bwide * 8 - THE_image.width * THE_image.bitsPerSample;
    if ( GLOBAL_loadFlags & 1 ) {
        bwide = bwide * 16 / 15;
    }
    bite = 8 + (GLOBAL_loadFlags & 56);
    half = (THE_image.height + 1) >> 1;
    for ( irow = 0; irow < THE_image.height; irow++ ) {
        row = irow;
        if ( GLOBAL_loadFlags & 2 &&
             (row = irow % half * 2 + irow / half) == 1 &&
             GLOBAL_loadFlags & 4 ) {
            if ( vbits = 0, tiff_compress ) {
                fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset - (-half * bwide & -2048), SEEK_SET);
            } else {
                fseek(GLOBAL_IO_ifp, 0, SEEK_END);
                fseek(GLOBAL_IO_ifp, ftell(GLOBAL_IO_ifp) >> 3 << 2, SEEK_SET);
            }
        }
        for ( col = 0; col < THE_image.width; col++ ) {
            for ( vbits -= THE_image.bitsPerSample; vbits < 0; vbits += bite ) {
                bitbuf <<= bite;
                for ( i = 0; i < bite; i += 8 ) {
                    bitbuf |= ((unsigned long long) fgetc(GLOBAL_IO_ifp) << i);
                }
            }
            val = bitbuf << (64 - THE_image.bitsPerSample - vbits) >> (64 - THE_image.bitsPerSample);
            RAW(row, col ^ (GLOBAL_loadFlags >> 6 & 3)) = val;
            if ( GLOBAL_loadFlags & 1 && (col % 10) == 9 && fgetc(GLOBAL_IO_ifp) &&
                 row < THE_image.height + top_margin && col < THE_image.width + left_margin ) {
                inputOutputError();
            }
        }
        vbits -= rbits;
    }
}
