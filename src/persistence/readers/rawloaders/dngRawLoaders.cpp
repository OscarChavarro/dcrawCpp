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
