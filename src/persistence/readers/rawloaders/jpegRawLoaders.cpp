#include "jpegRawLoaders.h"
#include "../globalsio.h"
#include "../../../common/globals.h"

unsigned short *
ljpeg_row(int jrow, struct jhead *jh) {
    int col;
    int c;
    int diff;
    int pred;
    int spred = 0;
    unsigned short mark = 0;
    unsigned short *row[3];

    if ( jrow * jh->wide % jh->restart == 0 ) {
        for ( c = 0; c < 6; c++ ) {
            jh->vpred[c] = 1 << (jh->bits - 1);
        }
        if ( jrow ) {
            fseek(GLOBAL_IO_ifp, -2, SEEK_CUR);
            do mark = (mark << 8) + (c = fgetc(GLOBAL_IO_ifp));
            while ( c != EOF && mark >> 4 != 0xffd );
        }
        getbits(-1);
    }
    for ( c = 0; c < 3; c++ ) {
        row[c] = jh->row + jh->wide * jh->clrs * ((jrow + c) & 1);
    }
    for ( col = 0; col < jh->wide; col++ ) {
        for ( c = 0; c < jh->clrs; c++ ) {
            diff = ljpeg_diff(jh->huff[c]);
            if ( jh->sraw && c <= jh->sraw && (col | c)) {
                pred = spred;
            } else {
                if ( col ) {
                    pred = row[0][-jh->clrs];
                } else {
                    pred = (jh->vpred[c] += diff) - diff;
                }
            }
            if ( jrow && col )
                switch ( jh->psv ) {
                    case 1:
                        break;
                    case 2:
                        pred = row[1][0];
                        break;
                    case 3:
                        pred = row[1][-jh->clrs];
                        break;
                    case 4:
                        pred = pred + row[1][0] - row[1][-jh->clrs];
                        break;
                    case 5:
                        pred = pred + ((row[1][0] - row[1][-jh->clrs]) >> 1);
                        break;
                    case 6:
                        pred = row[1][0] + ((pred - row[1][-jh->clrs]) >> 1);
                        break;
                    case 7:
                        pred = (pred + row[1][0]) >> 1;
                        break;
                    default:
                        pred = 0;
                }
            if ( (**row = pred + diff) >> jh->bits ) {
                inputOutputError();
            }
            if ( c <= jh->sraw ) {
                spred = **row;
            }
            row[0]++;
            row[1]++;
        }
    }
    return row[2];
}

int
ljpeg_diff(unsigned short *huff) {
    int len;
    int diff;

    len = gethuff(huff);
    if ( len == 16 && (!GLOBAL_dngVersion || GLOBAL_dngVersion >= 0x1010000) ) {
        return -32768;
    }
    diff = getbits(len);
    if ( (diff & (1 << (len - 1))) == 0 ) {
        diff -= (1 << len) - 1;
    }
    return diff;
}

