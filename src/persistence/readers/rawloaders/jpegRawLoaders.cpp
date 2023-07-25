#include <climits>
#include <cstring>
#include <cstdlib>
#include "jpegRawLoaders.h"
#include "../globalsio.h"
#include "../../../common/globals.h"
#include "../../../common/util.h"

int
ljpeg_start(struct jhead *jh, int info_only) {
    unsigned short c;
    unsigned short tag;
    unsigned short len;
    unsigned char data[0x10000];
    const unsigned char *dp;

    memset(jh, 0, sizeof *jh);
    jh->restart = INT_MAX;
    if ((fgetc(GLOBAL_IO_ifp), fgetc(GLOBAL_IO_ifp)) != 0xd8 ) {
        return 0;
    }
    do {
        if ( !fread(data, 2, 2, GLOBAL_IO_ifp)) {
            return 0;
        }
        tag = data[0] << 8 | data[1];
        len = (data[2] << 8 | data[3]) - 2;
        if ( tag <= 0xff00 ) {
            return 0;
        }
        fread(data, 1, len, GLOBAL_IO_ifp);
        switch ( tag ) {
            case 0xffc3:
                jh->sraw = ((data[7] >> 4) * (data[7] & 15) - 1) & 3;
            case 0xffc1:
            case 0xffc0:
                jh->algo = tag & 0xff;
                jh->bits = data[0];
                jh->high = data[1] << 8 | data[2];
                jh->wide = data[3] << 8 | data[4];
                jh->clrs = data[5] + jh->sraw;
                if ( len == 9 && !GLOBAL_dngVersion ) {
                    getc(GLOBAL_IO_ifp);
                }
                break;
            case 0xffc4:
                if ( info_only ) {
                    break;
                }
                for ( dp = data; dp < data + len && !((c = *dp++) & -20); ) {
                    jh->free[c] = jh->huff[c] = make_decoder_ref(&dp);
                }
                break;
            case 0xffda:
                jh->psv = data[1 + data[0] * 2];
                jh->bits -= data[3 + data[0] * 2] & 15;
                break;
            case 0xffdb:
                for ( c = 0; c < 64; c++ ) {
                    jh->quant[c] = data[c * 2 + 1] << 8 | data[c * 2 + 2];
                }
                break;
            case 0xffdd:
                jh->restart = data[0] << 8 | data[1];
        }
    }
    while ( tag != 0xffda );

    if ( jh->bits > 16 || jh->clrs > 6 || !jh->bits || !jh->high || !jh->wide || !jh->clrs ) {
        return 0;
    }
    if ( info_only ) {
        return 1;
    }
    if ( !jh->huff[0] ) {
        return 0;
    }
    for ( c = 0; c < 19; c++ ) {
        if ( !jh->huff[c + 1] ) {
            jh->huff[c + 1] = jh->huff[c];
        }
    }
    if ( jh->sraw ) {
        for ( c = 0; c < 4; c++ ) {
            jh->huff[2 + c] = jh->huff[1];
        }
        for ( c = 0; c < jh->sraw; c++ ) {
            jh->huff[1 + c] = jh->huff[0];
        }
    }
    jh->row = (unsigned short *) calloc(jh->wide * jh->clrs, 4);
    memoryError(jh->row, "ljpeg_start()");
    return GLOBAL_IO_zeroAfterFf = 1;
}

void
ljpeg_end(struct jhead *jh) {
    int c;

    for ( c = 0; c < 4; c++ ) {
        if ( jh->free[c] ) {
            free(jh->free[c]);
        }
    }
    free(jh->row);
}

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

