#include "../../../common/globals.h"
#include "../../../common/util.h"
#include "../../../common/Options.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../../../colorRepresentation/adobeCoeff.h"
#include "../globalsio.h"
#include "leafRawLoaders.h"

void
leaf_hdr_load_raw() {
    unsigned short *pixel = 0;
    unsigned tile = 0;
    unsigned r;
    unsigned c;
    unsigned row;
    unsigned col;

    if ( !IMAGE_filters ) {
        pixel = (unsigned short *) calloc(THE_image.width, sizeof *pixel);
        memoryError(pixel, "leaf_hdr_load_raw()");
    }
    for ( c = 0; c < tiff_samples; c++ ) {
        for ( r = 0; r < THE_image.height; r++ ) {
            if ( r % tile_length == 0 ) {
                fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset + 4 * tile++, SEEK_SET);
                fseek(GLOBAL_IO_ifp, read4bytes(), SEEK_SET);
            }
            if ( IMAGE_filters && c != OPTIONS_values->shotSelect ) {
                continue;
            }
            if ( IMAGE_filters ) {
                pixel = THE_image.rawData + r * THE_image.width;
            }
            readShorts(pixel, THE_image.width);
            if ( !IMAGE_filters && (row = r - top_margin) < height ) {
                for ( col = 0; col < width; col++ ) {
                    GLOBAL_image[row * width + col][c] = pixel[col + left_margin];
                }
            }
        }
    }
    if ( !IMAGE_filters ) {
        ADOBE_maximum = 0xffff;
        GLOBAL_colorTransformForRaw = 1;
        free(pixel);
    }
}
