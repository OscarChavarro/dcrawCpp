#include "../../../common/globals.h"
#include "../../../common/util.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../../../colorRepresentation/adobeCoeff.h"
#include "../globalsio.h"
#include "rolleiRawLoaders.h"

void
rollei_thumb() {
    unsigned i;
    unsigned short *thumb;

    thumb_length = thumb_width * thumb_height;
    thumb = (unsigned short *) calloc(thumb_length, 2);
    memoryError(thumb, "rollei_thumb()");
    fprintf(ofp, "P6\n%d %d\n255\n", thumb_width, thumb_height);
    readShorts(thumb, thumb_length);
    for ( i = 0; i < thumb_length; i++ ) {
        putc(thumb[i] << 3, ofp);
        putc(thumb[i] >> 5 << 2, ofp);
        putc(thumb[i] >> 11 << 3, ofp);
    }
    free(thumb);
}

void
rollei_load_raw() {
    unsigned char pixel[10];
    unsigned iten = 0;
    unsigned isix;
    unsigned i;
    unsigned buffer = 0;
    unsigned todo[16];

    isix = THE_image.width * THE_image.height * 5 / 8;
    while ( fread(pixel, 1, 10, GLOBAL_IO_ifp) == 10 ) {
        for ( i = 0; i < 10; i += 2 ) {
            todo[i] = iten++;
            todo[i + 1] = pixel[i] << 8 | pixel[i + 1];
            buffer = pixel[i] >> 2 | buffer << 6;
        }
        for ( ; i < 16; i += 2 ) {
            todo[i] = isix++;
            todo[i + 1] = buffer >> (14 - i) * 5;
        }
        for ( i = 0; i < 16; i += 2 ) {
            THE_image.rawData[todo[i]] = (todo[i + 1] & 0x3ff);
        }
    }
    ADOBE_maximum = 0x3ff;
}
