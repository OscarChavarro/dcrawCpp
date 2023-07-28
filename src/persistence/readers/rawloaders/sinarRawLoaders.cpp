#include "../../../common/globals.h"
#include "../../../common/mathMacros.h"
#include "../../../common/util.h"
#include "../../../common/Options.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../globalsio.h"
#include "sinarRawLoaders.h"
#include "standardRawLoaders.h"

void
sinar_4shot_load_raw() {
    unsigned short *pixel;
    unsigned shot;
    unsigned row;
    unsigned col;
    unsigned r;
    unsigned c;

    if ( THE_image.rawData ) {
        shot = LIM (OPTIONS_values->shotSelect, 1, 4) - 1;
        fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset + shot * 4, SEEK_SET);
        fseek(GLOBAL_IO_ifp, read4bytes(), SEEK_SET);
        unpacked_load_raw();
        return;
    }
    pixel = (unsigned short *) calloc(THE_image.width, sizeof *pixel);
    memoryError(pixel, "sinar_4shot_load_raw()");
    for ( shot = 0; shot < 4; shot++ ) {
        fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset + shot * 4, SEEK_SET);
        fseek(GLOBAL_IO_ifp, read4bytes(), SEEK_SET);
        for ( row = 0; row < THE_image.height; row++ ) {
            readShorts(pixel, THE_image.width);
            if ( (r = row - top_margin - (shot >> 1 & 1)) >= height ) {
                continue;
            }
            for ( col = 0; col < THE_image.width; col++ ) {
                if ( (c = col - left_margin - (shot & 1)) >= width ) {
                    continue;
                }
                GLOBAL_image[r * width + c][(row & 1) * 3 ^ (~col & 1)] = pixel[col];
            }
        }
    }
    free(pixel);
    mix_green = 1;
}
