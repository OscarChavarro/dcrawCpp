#include <climits>
#include "../../../common/globals.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../../../common/mathMacros.h"
#include "../../../common/util.h"
#include "../globalsio.h"
#include "jpegRawLoaders.h"
#include "dngRawLoaders.h"

void
packed_dng_load_raw() {
    unsigned short *pixel;
    unsigned short *rp;
    int row;
    int col;

    pixel = (unsigned short *) calloc(THE_image.width, tiff_samples * sizeof *pixel);
    memoryError(pixel, "packed_dng_load_raw()");
    for ( row = 0; row < THE_image.height; row++ ) {
        if ( THE_image.bitsPerSample == 16 ) {
            readShorts(pixel, THE_image.width * tiff_samples);
        } else {
            getbits(-1);
            for ( col = 0; col < THE_image.width * tiff_samples; col++ ) {
                pixel[col] = getbits(THE_image.bitsPerSample);
            }
        }
        for ( rp = pixel, col = 0; col < THE_image.width; col++ ) {
            adobe_copy_pixel(row, col, &rp);
        }
    }
    free(pixel);
}

void
lossless_dng_load_raw() {
    unsigned save;
    unsigned trow = 0;
    unsigned tcol = 0;
    unsigned jwide;
    unsigned jrow;
    unsigned jcol;
    unsigned row;
    unsigned col;
    unsigned i;
    unsigned j;
    struct jhead jh;
    unsigned short *rp;

    while ( trow < THE_image.height ) {
        save = ftell(GLOBAL_IO_ifp);
        if ( tile_length < INT_MAX ) {
            fseek(GLOBAL_IO_ifp, read4bytes(), SEEK_SET);
        }
        if ( !ljpeg_start(&jh, 0)) {
            break;
        }
        jwide = jh.wide;
        if ( IMAGE_filters ) {
            jwide *= jh.clrs;
        }
        jwide /= MIN (is_raw, tiff_samples);
        switch ( jh.algo ) {
            case 0xc1:
                jh.vpred[0] = 16384;
                getbits(-1);
                for ( jrow = 0; jrow + 7 < jh.high; jrow += 8 ) {
                    for ( jcol = 0; jcol + 7 < jh.wide; jcol += 8 ) {
                        ljpeg_idct(&jh);
                        rp = jh.idct;
                        row = trow + jcol / tile_width + jrow * 2;
                        col = tcol + jcol % tile_width;
                        for ( i = 0; i < 16; i += 2 ) {
                            for ( j = 0; j < 8; j++ ) {
                                adobe_copy_pixel(row + i, col + j, &rp);
                            }
                        }
                    }
                }
                break;
            case 0xc3:
                for ( row = col = jrow = 0; jrow < jh.high; jrow++ ) {
                    rp = ljpeg_row(jrow, &jh);
                    for ( jcol = 0; jcol < jwide; jcol++ ) {
                        adobe_copy_pixel(trow + row, tcol + col, &rp);
                        if ( ++col >= tile_width || col >= THE_image.width ) {
                            row += 1 + (col = 0);
                        }
                    }
                }
        }
        fseek(GLOBAL_IO_ifp, save + 4, SEEK_SET);
        if ((tcol += tile_width) >= THE_image.width ) {
            trow += tile_length + (tcol = 0);
        }
        ljpeg_end(&jh);
    }
}
