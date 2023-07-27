#include "../../../common/globals.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../../../common/mathMacros.h"
#include "../globalsio.h"
#include "jpegRawLoaders.h"
#include "samsungRawLoaders.h"

void
samsung_load_raw() {
    int row;
    int col;
    int c;
    int i;
    int dir;
    int op[4];
    int len[4];

    GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
    for ( row = 0; row < THE_image.height; row++ ) {
        fseek(GLOBAL_IO_ifp, strip_offset + row * 4, SEEK_SET);
        fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset + read4bytes(), SEEK_SET);
        ph1_bits(-1);
        for ( c = 0; c < 4; c++ ) {
            len[c] = row < 2 ? 7 : 4;
        }
        for ( col = 0; col < THE_image.width; col += 16 ) {
            dir = ph1_bits(1);
            for ( c = 0; c < 4; c++ ) {
                op[c] = ph1_bits(2);
            }
            for ( c = 0; c < 4; c++ ) {
                switch ( op[c] ) {
                    case 3:
                        len[c] = ph1_bits(4);
                        break;
                    case 2:
                        len[c]--;
                        break;
                    case 1:
                        len[c]++;
                }
            }
            for ( c = 0; c < 16; c += 2 ) {
                i = len[((c & 1) << 1) | (c >> 3)];
                RAW(row, col + c) = ((signed) ph1_bits(i) << (32 - i) >> (32 - i)) +
                                    (dir ? RAW(row + (~c | -2), col + c) : col ? RAW(row, col + (c | -2)) : 128);
                if ( c == 14 ) {
                    c = -1;
                }
            }
        }
    }
    for ( row = 0; row < THE_image.height - 1; row += 2 ) {
        for ( col = 0; col < THE_image.width - 1; col += 2 ) {
            SWAP(RAW(row, col + 1), RAW(row + 1, col));
        }
    }
}

void
samsung2_load_raw() {
    static const unsigned short tab[14] =
            {0x304, 0x307, 0x206, 0x205, 0x403, 0x600, 0x709,
             0x80a, 0x90b, 0xa0c, 0xa0d, 0x501, 0x408, 0x402};
    unsigned short huff[1026];
    unsigned short vpred[2][2] = {{0, 0},
                                  {0, 0}};
    unsigned short hpred[2];
    int i;
    int c;
    int n;
    int row;
    int col;
    int diff;

    huff[0] = 10;
    for ( n = i = 0; i < 14; i++ ) {
        for ( c = 0; c < 1024 >> (tab[i] >> 8); c++ ) {
            huff[++n] = tab[i];
        }
    }
    getbits(-1);
    for ( row = 0; row < THE_image.height; row++ ) {
        for ( col = 0; col < THE_image.width; col++ ) {
            diff = ljpeg_diff(huff);
            if ( col < 2 ) {
                hpred[col] = vpred[row & 1][col] += diff;
            } else {
                hpred[col & 1] += diff;
            }
            RAW(row, col) = hpred[col & 1];
            if ( hpred[col & 1] >> THE_image.bitsPerSample ) {
                inputOutputError();
            }
        }
    }
}

void
samsung3_load_raw() {
    int opt;
    int init;
    int mag;
    int pmode;
    int row;
    int tab;
    int col;
    int pred;
    int diff;
    int i;
    int c;
    unsigned short lent[3][2];
    unsigned short len[4];
    unsigned short *prow[2];

    GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
    fseek(GLOBAL_IO_ifp, 9, SEEK_CUR);
    opt = fgetc(GLOBAL_IO_ifp);
    init = (read2bytes(), read2bytes());
    for ( row = 0; row < THE_image.height; row++ ) {
        fseek(GLOBAL_IO_ifp, (GLOBAL_IO_profileOffset - ftell(GLOBAL_IO_ifp)) & 15, SEEK_CUR);
        ph1_bits(-1);
        mag = 0;
        pmode = 7;
        for ( c = 0; c < 6; c++ ) {
            ((unsigned short *) lent)[c] = row < 2 ? 7 : 4;
        }
        prow[row & 1] = &RAW(row - 1, 1 - ((row & 1) << 1));    // green
        prow[~row & 1] = &RAW(row - 2, 0);            // red and blue
        for ( tab = 0; tab + 15 < THE_image.width; tab += 16 ) {
            if ( ~opt & 4 && !(tab & 63)) {
                i = ph1_bits(2);
                mag = i < 3 ? mag - '2' + "204"[i] : ph1_bits(12);
            }
            if ( opt & 2 ) {
                pmode = 7 - 4 * ph1_bits(1);
            } else {
                if ( !ph1_bits(1)) {
                    pmode = ph1_bits(3);
                }
            }
            if ( opt & 1 || !ph1_bits(1)) {
                for ( c = 0; c < 4; c++ ) {
                    len[c] = ph1_bits(2);
                }
                for ( c = 0; c < 4; c++ ) {
                    i = ((row & 1) << 1 | (c & 1)) % 3;
                    len[c] = len[c] < 3 ? lent[i][0] - '1' + "120"[len[c]] : ph1_bits(4);
                    lent[i][0] = lent[i][1];
                    lent[i][1] = len[c];
                }
            }
            for ( c = 0; c < 16; c++ ) {
                col = tab + (((c & 7) << 1) ^ (c >> 3) ^ (row & 1));
                pred = (pmode == 7 || row < 2)
                       ? (tab ? RAW(row, tab - 2 + (col & 1)) : init)
                       : (prow[col & 1][col - '4' + "0224468"[pmode]] +
                          prow[col & 1][col - '4' + "0244668"[pmode]] + 1) >> 1;
                diff = ph1_bits (i = len[c >> 2]);
                if ( diff >> (i - 1)) {
                    diff -= 1 << i;
                }
                diff = diff * (mag * 2 + 1) + mag;
                RAW(row, col) = pred + diff;
            }
        }
    }
}
