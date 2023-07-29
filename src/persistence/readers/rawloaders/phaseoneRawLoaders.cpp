#include <cmath>
#include <climits>
#include "../../../common/globals.h"
#include "../../../common/mathMacros.h"
#include "../../../common/util.h"
#include "../../../common/Options.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../../../colorRepresentation/adobeCoeff.h"
#include "../../../postprocessors/gamma.h"
#include "../globalsio.h"
#include "phaseoneRawLoaders.h"

static void
phase_one_flat_field(int is_float, int nc) {
    unsigned short head[8];
    unsigned wide;
    unsigned high;
    unsigned y;
    unsigned x;
    unsigned c;
    unsigned rend;
    unsigned cend;
    unsigned row;
    unsigned col;
    float *mrow;
    float num;
    float mult[4];

    readShorts(head, 8);
    if ( head[2] * head[3] * head[4] * head[5] == 0 ) {
        return;
    }
    wide = head[2] / head[4] + (head[2] % head[4] != 0);
    high = head[3] / head[5] + (head[3] % head[5] != 0);
    mrow = (float *)calloc(nc * wide, sizeof *mrow);
    memoryError(mrow, "phase_one_flat_field()");
    for ( y = 0; y < high; y++ ) {
        for ( x = 0; x < wide; x++ ) {
            for ( c = 0; c < nc; c += 2 ) {
                num = is_float ? readDouble(11) : read2bytes() / 32768.0;
                if ( y == 0 ) mrow[c * wide + x] = num;
                else mrow[(c + 1) * wide + x] = (num - mrow[c * wide + x]) / head[5];
            }
        }
        if ( y == 0 ) {
            continue;
        }
        rend = head[1] + y * head[5];
        for ( row = rend - head[5];
              row < THE_image.height && row < rend &&
              row < head[1] + head[3] - head[5]; row++ ) {
            for ( x = 1; x < wide; x++ ) {
                for ( c = 0; c < nc; c += 2 ) {
                    mult[c] = mrow[c * wide + x - 1];
                    mult[c + 1] = (mrow[c * wide + x] - mult[c]) / head[4];
                }
                cend = head[0] + x * head[4];
                for ( col = cend - head[4];
                      col < THE_image.width &&
                      col < cend && col < head[0] + head[2] - head[4]; col++ ) {
                    c = nc > 2 ? FC(row - top_margin, col - left_margin) : 0;
                    if ( !(c & 1)) {
                        c = RAW(row, col) * mult[c];
                        RAW(row, col) = LIM(c, 0, 65535);
                    }
                    for ( c = 0; c < nc; c += 2 ) {
                        mult[c] += mult[c + 1];
                    }
                }
            }
            for ( x = 0; x < wide; x++ ) {
                for ( c = 0; c < nc; c += 2 ) {
                    mrow[c * wide + x] += mrow[(c + 1) * wide + x];
                }
            }
        }
    }
    free(mrow);
}

void
phase_one_load_raw() {
    int a;
    int b;
    int i;
    unsigned short akey, bkey, mask;

    fseek(GLOBAL_IO_ifp, ph1.key_off, SEEK_SET);
    akey = read2bytes();
    bkey = read2bytes();
    mask = ph1.format == 1 ? 0x5555 : 0x1354;
    fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset, SEEK_SET);
    readShorts(THE_image.rawData, THE_image.width * THE_image.height);
    if ( ph1.format ) {
        for ( i = 0; i < THE_image.width * THE_image.height; i += 2 ) {
            a = THE_image.rawData[i + 0] ^ akey;
            b = THE_image.rawData[i + 1] ^ bkey;
            THE_image.rawData[i + 0] = (a & mask) | (b & ~mask);
            THE_image.rawData[i + 1] = (b & mask) | (a & ~mask);
        }
    }
}

void
phase_one_load_raw_c() {
    static const int length[] = {8, 7, 6, 9, 11, 10, 5, 12, 14, 13};
    int *offset;
    int len[2];
    int pred[2];
    int row;
    int col;
    int i;
    int j;
    unsigned short *pixel;
    short (*cblack)[2];
    short (*rblack)[2];

    pixel = (unsigned short *) calloc(THE_image.width * 3 + THE_image.height * 4, 2);
    memoryError(pixel, "phase_one_load_raw_c()");
    offset = (int *) (pixel + THE_image.width);
    fseek(GLOBAL_IO_ifp, strip_offset, SEEK_SET);
    for ( row = 0; row < THE_image.height; row++ ) {
        offset[row] = read4bytes();
    }
    cblack = (short (*)[2]) (offset + THE_image.height);
    fseek(GLOBAL_IO_ifp, ph1.black_col, SEEK_SET);
    if ( ph1.black_col ) {
        readShorts((unsigned short *) cblack[0], THE_image.height * 2);
    }
    rblack = cblack + THE_image.height;
    fseek(GLOBAL_IO_ifp, ph1.black_row, SEEK_SET);
    if ( ph1.black_row ) {
        readShorts((unsigned short *) rblack[0], THE_image.width * 2);
    }
    for ( i = 0; i < 256; i++ ) {
        GAMMA_curveFunctionLookupTable[i] = i * i / 3.969 + 0.5;
    }
    for ( row = 0; row < THE_image.height; row++ ) {
        fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset + offset[row], SEEK_SET);
        ph1_bits(-1);
        pred[0] = pred[1] = 0;
        for ( col = 0; col < THE_image.width; col++ ) {
            if ( col >= (THE_image.width & -8) ) {
                len[0] = len[1] = 14;
            } else {
                if ((col & 7) == 0 ) {
                    for ( i = 0; i < 2; i++ ) {
                        for ( j = 0; j < 5 && !ph1_bits(1); j++ );
                        if ( j-- ) {
                            len[i] = length[j * 2 + ph1_bits(1)];
                        }
                    }
                }
            }
            if ((i = len[col & 1]) == 14 ) {
                pixel[col] = pred[col & 1] = ph1_bits(16);
            } else {
                pixel[col] = pred[col & 1] += ph1_bits(i) + 1 - (1 << (i - 1));
            }
            if ( pred[col & 1] >> 16 ) {
                inputOutputError();
            }
            if ( ph1.format == 5 && pixel[col] < 256 ) {
                pixel[col] = GAMMA_curveFunctionLookupTable[pixel[col]];
            }
        }
        for ( col = 0; col < THE_image.width; col++ ) {
            i = (pixel[col] << 2 * (ph1.format != 8)) - ph1.black
                + cblack[row][col >= ph1.split_col]
                + rblack[col][row >= ph1.split_row];
            if ( i > 0 ) {
                RAW(row, col) = i;
            }
        }
    }
    free(pixel);
    ADOBE_maximum = 0xfffc - ph1.black;
}

void
phase_one_correct() {
    unsigned entries;
    unsigned tag;
    unsigned data;
    unsigned save;
    unsigned col;
    unsigned row;
    unsigned type;
    int len;
    int i;
    int j;
    int k;
    int cip;
    int val[4];
    int dev[4];
    int sum;
    int max;
    int head[9];
    int diff;
    int mindiff = INT_MAX;
    int off_412 = 0;
    static const signed char dir[12][2] =
            {{-1, -1},
             {-1, 1},
             {1,  -1},
             {1,  1},
             {-2, 0},
             {0,  -2},
             {0,  2},
             {2,  0},
             {-2, -2},
             {-2, 2},
             {2,  -2},
             {2,  2}};
    float poly[8];
    float num;
    float cfrac;
    float frac;
    float mult[2];
    float *yval[2];
    unsigned short *xval[2];
    int qmult_applied = 0;
    int qlin_applied = 0;

    if ( OPTIONS_values->halfSizePreInterpolation || !meta_length ) {
        return;
    }
    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, _("Phase One correction...\n"));
    }
    fseek(GLOBAL_IO_ifp, GLOBAL_meta_offset, SEEK_SET);
    GLOBAL_endianOrder = read2bytes();
    fseek(GLOBAL_IO_ifp, 6, SEEK_CUR);
    fseek(GLOBAL_IO_ifp, GLOBAL_meta_offset + read4bytes(), SEEK_SET);
    entries = read4bytes();
    read4bytes();
    while ( entries-- ) {
        tag = read4bytes();
        len = read4bytes();
        data = read4bytes();
        save = ftell(GLOBAL_IO_ifp);
        fseek(GLOBAL_IO_ifp, GLOBAL_meta_offset + data, SEEK_SET);
        if ( tag == 0x419 ) {
            // Polynomial curve
            for ( read4bytes(), i = 0; i < 8; i++ ) {
                poly[i] = readDouble(11);
            }
            poly[3] += (ph1.tag_210 - poly[7]) * poly[6] + 1;
            for ( i = 0; i < 0x10000; i++ ) {
                num = (poly[5] * i + poly[3]) * i + poly[1];
                GAMMA_curveFunctionLookupTable[i] = LIM(num, 0, 65535);
            }
            goto apply; // apply to right half
        } else {
            if ( tag == 0x41a ) {
                // Polynomial curve
                for ( i = 0; i < 4; i++ ) {
                    poly[i] = readDouble(11);
                }
                for ( i = 0; i < 0x10000; i++ ) {
                    for ( num = 0, j = 4; j--; ) {
                        num = num * i + poly[j];
                    }
                    GAMMA_curveFunctionLookupTable[i] = LIM(num + i, 0, 65535);
                }
                apply: // apply to whole GLOBAL_image
                for ( row = 0; row < THE_image.height; row++ ) {
                    for ( col = (tag & 1) * ph1.split_col; col < THE_image.width; col++ ) {
                        RAW(row, col) = GAMMA_curveFunctionLookupTable[RAW(row, col)];
                    }
                }
            } else {
                if ( tag == 0x400 ) {
                    // Sensor defects
                    while ((len -= 8) >= 0 ) {
                        col = read2bytes();
                        row = read2bytes();
                        type = read2bytes();
                        read2bytes();
                        if ( col >= THE_image.width ) {
                            continue;
                        }
                        if ( type == 131 || type == 137 ) {
                            // Bad column
                            for ( row = 0; row < THE_image.height; row++ ) {
                                if ( FC(row - top_margin, col - left_margin) == 1 ) {
                                    for ( sum = i = 0; i < 4; i++ ) {
                                        sum += val[i] = raw(row + dir[i][0], col + dir[i][1]);
                                    }
                                    for ( max = i = 0; i < 4; i++ ) {
                                        dev[i] = abs((val[i] << 2) - sum);
                                        if ( dev[max] < dev[i] ) {
                                            max = i;
                                        }
                                    }
                                    RAW(row, col) = (sum - val[max]) / 3.0 + 0.5;
                                } else {
                                    for ( sum = 0, i = 8; i < 12; i++ ) {
                                        sum += raw(row + dir[i][0], col + dir[i][1]);
                                    }
                                    RAW(row, col) = 0.5 + sum * 0.0732233 +
                                                    (raw(row, col - 2) + raw(row, col + 2)) * 0.3535534;
                                }
                            }
                        } else {
                            if ( type == 129 ) {
                                // Bad pixel
                                if ( row >= THE_image.height ) continue;
                                j = (FC(row - top_margin, col - left_margin) != 1) * 4;
                                for ( sum = 0, i = j; i < j + 8; i++ )
                                    sum += raw(row + dir[i][0], col + dir[i][1]);
                                RAW(row, col) = (sum + 4) >> 3;
                            }
                        }
                    }
                } else {
                    if ( tag == 0x401 ) {
                        // All-color flat fields
                        phase_one_flat_field(1, 2);
                    } else {
                        if ( tag == 0x416 || tag == 0x410 ) {
                            phase_one_flat_field(0, 2);
                        } else {
                            if ( tag == 0x40b ) {            /* Red+blue flat field */
                                phase_one_flat_field(0, 4);
                            } else {
                                if ( tag == 0x412 ) {
                                    fseek(GLOBAL_IO_ifp, 36, SEEK_CUR);
                                    diff = abs(read2bytes() - ph1.tag_21a);
                                    if ( mindiff > diff ) {
                                        mindiff = diff;
                                        off_412 = ftell(GLOBAL_IO_ifp) - 38;
                                    }
                                } else {
                                    if ( tag == 0x41f && !qlin_applied ) {
                                        // Quadrant linearization
                                        unsigned short lc[2][2][16], ref[16];
                                        int qr, qc;
                                        for ( qr = 0; qr < 2; qr++ ) {
                                            for ( qc = 0; qc < 2; qc++ ) {
                                                for ( i = 0; i < 16; i++ ) {
                                                    lc[qr][qc][i] = read4bytes();
                                                }
                                            }
                                        }
                                        for ( i = 0; i < 16; i++ ) {
                                            int v = 0;
                                            for ( qr = 0; qr < 2; qr++ )
                                                for ( qc = 0; qc < 2; qc++ )
                                                    v += lc[qr][qc][i];
                                            ref[i] = (v + 2) >> 2;
                                        }
                                        for ( qr = 0; qr < 2; qr++ ) {
                                            for ( qc = 0; qc < 2; qc++ ) {
                                                int cx[19], cf[19];
                                                for ( i = 0; i < 16; i++ ) {
                                                    cx[1 + i] = lc[qr][qc][i];
                                                    cf[1 + i] = ref[i];
                                                }
                                                cx[0] = cf[0] = 0;
                                                cx[17] = cf[17] = ((unsigned) ref[15] * 65535) / lc[qr][qc][15];
                                                cx[18] = cf[18] = 65535;
                                                cubic_spline(cx, cf, 19);
                                                for ( row = (qr ? ph1.split_row : 0);
                                                      row < (qr ? THE_image.height : ph1.split_row); row++ ) {
                                                    for ( col = (qc ? ph1.split_col : 0);
                                                          col < (qc ? THE_image.width : ph1.split_col); col++ ) {
                                                        RAW(row, col) = GAMMA_curveFunctionLookupTable[RAW(row, col)];
                                                    }
                                                }
                                            }
                                        }
                                        qlin_applied = 1;
                                    } else {
                                        if ( tag == 0x41e && !qmult_applied ) {
                                            // Quadrant multipliers
                                            float qmult[2][2] = {{1, 1},
                                                                 {1, 1}};
                                            read4bytes();
                                            read4bytes();
                                            read4bytes();
                                            read4bytes();
                                            qmult[0][0] = 1.0 + readDouble(11);
                                            read4bytes();
                                            read4bytes();
                                            read4bytes();
                                            read4bytes();
                                            read4bytes();
                                            qmult[0][1] = 1.0 + readDouble(11);
                                            read4bytes();
                                            read4bytes();
                                            read4bytes();
                                            qmult[1][0] = 1.0 + readDouble(11);
                                            read4bytes();
                                            read4bytes();
                                            read4bytes();
                                            qmult[1][1] = 1.0 + readDouble(11);
                                            for ( row = 0; row < THE_image.height; row++ ) {
                                                for ( col = 0; col < THE_image.width; col++ ) {
                                                    i = qmult[row >= ph1.split_row][col >= ph1.split_col] *
                                                        RAW(row, col);
                                                    RAW(row, col) = LIM(i, 0, 65535);
                                                }
                                            }
                                            qmult_applied = 1;
                                        } else {
                                            if ( tag == 0x431 && !qmult_applied ) {
                                                // Quadrant combined
                                                unsigned short lc[2][2][7], ref[7];
                                                int qr, qc;
                                                for ( i = 0; i < 7; i++ ) {
                                                    ref[i] = read4bytes();
                                                }
                                                for ( qr = 0; qr < 2; qr++ ) {
                                                    for ( qc = 0; qc < 2; qc++ ) {
                                                        for ( i = 0; i < 7; i++ ) {
                                                            lc[qr][qc][i] = read4bytes();
                                                        }
                                                    }
                                                }
                                                for ( qr = 0; qr < 2; qr++ ) {
                                                    for ( qc = 0; qc < 2; qc++ ) {
                                                        int cx[9], cf[9];
                                                        for ( i = 0; i < 7; i++ ) {
                                                            cx[1 + i] = ref[i];
                                                            cf[1 + i] = ((unsigned) ref[i] * lc[qr][qc][i]) / 10000;
                                                        }
                                                        cx[0] = cf[0] = 0;
                                                        cx[8] = cf[8] = 65535;
                                                        cubic_spline(cx, cf, 9);
                                                        for ( row = (qr ? ph1.split_row : 0);
                                                              row < (qr ? THE_image.height : ph1.split_row); row++ ) {
                                                            for ( col = (qc ? ph1.split_col : 0);
                                                                  col < (qc ? THE_image.width : ph1.split_col); col++ ) {
                                                                RAW(row, col) = GAMMA_curveFunctionLookupTable[RAW(row, col)];
                                                            }
                                                        }
                                                    }
                                                }
                                                qmult_applied = 1;
                                                qlin_applied = 1;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        fseek(GLOBAL_IO_ifp, save, SEEK_SET);
    }
    if ( off_412 ) {
        fseek(GLOBAL_IO_ifp, off_412, SEEK_SET);
        for ( i = 0; i < 9; i++ ) {
            head[i] = read4bytes() & 0x7fff;
        }
        yval[0] = (float *) calloc(head[1] * head[3] + head[2] * head[4], 6);
        memoryError(yval[0], "phase_one_correct()");
        yval[1] = (float *) (yval[0] + head[1] * head[3]);
        xval[0] = (unsigned short *) (yval[1] + head[2] * head[4]);
        xval[1] = (unsigned short *) (xval[0] + head[1] * head[3]);
        read2bytes();
        for ( i = 0; i < 2; i++ ) {
            for ( j = 0; j < head[i + 1] * head[i + 3]; j++ ) {
                yval[i][j] = readDouble(11);
            }
        }
        for ( i = 0; i < 2; i++ ) {
            for ( j = 0; j < head[i + 1] * head[i + 3]; j++ ) {
                xval[i][j] = read2bytes();
            }
        }
        for ( row = 0; row < THE_image.height; row++ ) {
            for ( col = 0; col < THE_image.width; col++ ) {
                cfrac = (float) col * head[3] / THE_image.width;
                cfrac -= cip = cfrac;
                num = RAW(row, col) * 0.5;
                for ( i = cip; i < cip + 2; i++ ) {
                    for ( k = j = 0; j < head[1]; j++ ) {
                        if ( num < xval[0][k = head[1] * i + j] ) {
                            break;
                        }
                    }
                    frac = (j == 0 || j == head[1]) ? 0 :
                           (xval[0][k] - num) / (xval[0][k] - xval[0][k - 1]);
                    mult[i - cip] = yval[0][k - 1] * frac + yval[0][k] * (1 - frac);
                }
                i = ((mult[0] * (1 - cfrac) + mult[1] * cfrac) * row + num) * 2;
                RAW(row, col) = LIM(i, 0, 65535);
            }
        }
        free(yval[0]);
    }
}
