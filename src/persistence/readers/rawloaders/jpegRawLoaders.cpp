#include <climits>
#include <cstring>
#include <cstdlib>
#include "jpegRawLoaders.h"
#include "../../../common/globals.h"
#include "../../../common/util.h"
#include "../../../common/mathMacros.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../../../postprocessors/gamma.h"
#include "../globalsio.h"
#include "jpegRawLoaders.h"

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

void
lossless_jpeg_load_raw() {
    int jwide;
    int jrow;
    int jcol;
    int val;
    int jidx;
    int i;
    int j;
    int row = 0;
    int col = 0;
    struct jhead jh;
    unsigned short *rp;

    if ( !ljpeg_start(&jh, 0) ) {
        return;
    }
    jwide = jh.wide * jh.clrs;

    for ( jrow = 0; jrow < jh.high; jrow++ ) {
        rp = ljpeg_row(jrow, &jh);
        if ( GLOBAL_loadFlags & 1 ) {
            row = jrow & 1 ? THE_image.height - 1 - jrow / 2 : jrow / 2;
        }
        for ( jcol = 0; jcol < jwide; jcol++ ) {
            val = GAMMA_curveFunctionLookupTable[*rp++];
            if ( cr2_slice[0] ) {
                jidx = jrow * jwide + jcol;
                i = jidx / (cr2_slice[1] * THE_image.height);
                if ( (j = i >= cr2_slice[0]) ) {
                    i = cr2_slice[0];
                }
                jidx -= i * (cr2_slice[1] * THE_image.height);
                row = jidx / cr2_slice[1 + j];
                col = jidx % cr2_slice[1 + j] + i * cr2_slice[1];
            }
            if ( THE_image.width == 3984 && (col -= 2) < 0 ) {
                col += (row--, THE_image.width);
            }
            if ((unsigned) row < THE_image.height ) {
                RAW(row, col) = val;
            }
            if ( ++col >= THE_image.width ) {
                col = (row++, 0);
            }
        }
    }
    ljpeg_end(&jh);
}

void
ljpeg_idct(struct jhead *jh) {
    int c;
    int i;
    int j;
    int len;
    int skip;
    int coef;
    float work[3][8][8];
    static float cs[106] = {0};
    static const unsigned char zigzag[80] =
            {0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33,
             40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36,
             29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54,
             47, 55, 62, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63};

    if ( !cs[0] ) {
        for ( c = 0; c < 106; c++ ) {
            cs[c] = cos((c & 31) * M_PI / 16) / 2;
        }
    }
    memset(work, 0, sizeof work);
    work[0][0][0] = jh->vpred[0] += ljpeg_diff(jh->huff[0]) * jh->quant[0];
    for ( i = 1; i < 64; i++ ) {
        len = gethuff (jh->huff[16]);
        i += skip = len >> 4;
        if ( !(len &= 15) && skip < 15 ) {
            break;
        }
        coef = getbits(len);
        if ( (coef & (1 << (len - 1))) == 0 ) {
            coef -= (1 << len) - 1;
        }
        ((float *) work)[zigzag[i]] = coef * jh->quant[i];
    }
    for ( c = 0; c < 8; c++ ) {
        work[0][0][c] *= M_SQRT1_2;
    }
    for ( c = 0; c < 8; c++ ) {
        work[0][c][0] *= M_SQRT1_2;
    }
    for ( i = 0; i < 8; i++ ) {
        for ( j = 0; j < 8; j++ ) {
            for ( c = 0; c < 8; c++ ) {
                work[1][i][j] += work[0][i][c] * cs[(j * 2 + 1) * c];
            }
        }
    }
    for ( i = 0; i < 8; i++ ) {
        for ( j = 0; j < 8; j++ ) {
            for ( c = 0; c < 8; c++ ) {
                work[2][i][j] += work[1][c][j] * cs[(i * 2 + 1) * c];
            }
        }
    }

    for ( c = 0; c < 64; c++ ) {
        jh->idct[c] = CLIP(((float *) work[2])[c] + 0.5);
    }
}
