#include "../../../common/globals.h"
#include "../globalsio.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../../../postprocessors/gamma.h"
#include "../../../common/mathMacros.h"
#include "../../../common/util.h"
#include "../../../common/Options.h"
#include "jpegRawLoaders.h"
#include "hasselbladRawLoaders.h"

void
hasselblad_load_raw() {
    struct jhead jh;
    int shot;
    int row;
    int col;
    int *back[5];
    int len[2];
    int diff[12];
    int pred;
    int sh;
    int f;
    int s;
    int c;
    unsigned upix;
    unsigned urow;
    unsigned ucol;
    unsigned short *ip;

    if ( !ljpeg_start(&jh, 0) ) {
        return;
    }
    GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
    ph1_bits(-1);
    back[4] = (int *) calloc(THE_image.width, 3 * sizeof **back);
    memoryError(back[4], "hasselblad_load_raw()");
    for ( c = 0; c < 3; c++ ) {
        back[c] = back[4] + c * THE_image.width;
    }
    cblack[6] >>= sh = tiff_samples > 1;
    shot = LIM(OPTIONS_values->shotSelect, 1, tiff_samples) - 1;
    for ( row = 0; row < THE_image.height; row++ ) {
        for ( c = 0; c < 4; c++ ) {
            back[(c + 3) & 3] = back[c];
        }
        for ( col = 0; col < THE_image.width; col += 2 ) {
            for ( s = 0; s < tiff_samples * 2; s += 2 ) {
                for ( c = 0; c < 2; c++ ) {
                    len[c] = ph1_huff(jh.huff[0]);
                }
                for ( c = 0; c < 2; c++ ) {
                    diff[s + c] = ph1_bits(len[c]);
                    if ((diff[s + c] & (1 << (len[c] - 1))) == 0 )
                        diff[s + c] -= (1 << len[c]) - 1;
                    if ( diff[s + c] == 65535 ) diff[s + c] = -32768;
                }
            }
            for ( s = col; s < col + 2; s++ ) {
                pred = 0x8000 + GLOBAL_loadFlags;
                if ( col ) pred = back[2][s - 2];
                if ( col && row > 1 )
                    switch ( jh.psv ) {
                        case 11:
                            pred += back[0][s] / 2 - back[0][s - 2] / 2;
                            break;
                    }
                f = (row & 1) * 3 ^ ((col + s) & 1);
                for ( c = 0; c < tiff_samples; c++ ) {
                    pred += diff[(s & 1) * tiff_samples + c];
                    upix = pred >> sh & 0xffff;
                    if ( THE_image.rawData && c == shot ) {
                        RAW(row, s) = upix;
                    }
                    if ( GLOBAL_image ) {
                        urow = row - top_margin + (c & 1);
                        ucol = col - left_margin - ((c >> 1) & 1);
                        ip = &GLOBAL_image[urow * THE_image.width + ucol][f];
                        if ( urow < THE_image.height && ucol < THE_image.width ) {
                            *ip = c < 4 ? upix : (*ip + upix) >> 1;
                        }
                    }
                }
                back[2][s] = pred;
            }
        }
    }
    free(back[4]);
    ljpeg_end(&jh);
    if ( GLOBAL_image ) {
        mix_green = 1;
    }
}
