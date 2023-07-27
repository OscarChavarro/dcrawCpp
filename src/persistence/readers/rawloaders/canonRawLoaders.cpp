#include <cstring>
#include <cctype>
#include "../../../common/globals.h"
#include "../../../common/mathMacros.h"
#include "../../../common/util.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../../../colorRepresentation/adobeCoeff.h"
#include "../../../postprocessors/gamma.h"
#include "../globalsio.h"
#include "jpegRawLoaders.h"
#include "canonRawLoaders.h"

/*
Return 0 if the GLOBAL_image starts with compressed data,
1 if it starts with uncompressed low-order bits.

In Canon compressed data, 0xff is always followed by 0x00.
*/
static int
canonHasLowBits() {
    unsigned char test[0x4000];
    int ret = 1;
    int i;

    fseek(GLOBAL_IO_ifp, 0, SEEK_SET);
    fread(test, 1, sizeof test, GLOBAL_IO_ifp);
    for ( i = 540; i < sizeof test - 1; i++ ) {
        if ( test[i] == 0xff ) {
            if ( test[i + 1] ) {
                return 1;
            }
            ret = 0;
        }
    }
    return ret;
}

void
canon_600_fixed_wb(int temp) {
    static const short mul[4][5] = {
            {667,  358, 397, 565, 452},
            {731,  390, 367, 499, 517},
            {1119, 396, 348, 448, 537},
            {1399, 485, 431, 508, 688}};
    int lo;
    int hi;
    int i;
    float frac = 0;

    for ( lo = 4; --lo; ) {
        if ( *mul[lo] <= temp ) {
            break;
        }
    }
    for ( hi = 0; hi < 3; hi++ ) {
        if ( *mul[hi] >= temp ) {
            break;
        }
    }
    if ( lo != hi ) {
        frac = (float) (temp - *mul[lo]) / (*mul[hi] - *mul[lo]);
    }
    for ( i = 1; i < 5; i++ ) {
        pre_mul[i - 1] = 1 / (frac * mul[hi][i] + (1 - frac) * mul[lo][i]);
    }
}

/*
Return values:  0 = white  1 = near white  2 = not white
*/
int
canon_600_color(int ratio[2], int mar) {
    int clipped = 0;
    int target;
    int miss;

    if ( flash_used ) {
        if ( ratio[1] < -104 ) {
            ratio[1] = -104;
            clipped = 1;
        }
        if ( ratio[1] > 12 ) {
            ratio[1] = 12;
            clipped = 1;
        }
    } else {
        if ( ratio[1] < -264 || ratio[1] > 461 ) {
            return 2;
        }
        if ( ratio[1] < -50 ) {
            ratio[1] = -50;
            clipped = 1;
        }
        if ( ratio[1] > 307 ) {
            ratio[1] = 307;
            clipped = 1;
        }
    }
    target = flash_used || ratio[1] < 197
             ? -38 - (398 * ratio[1] >> 10)
             : -123 + (48 * ratio[1] >> 10);
    if ( target - mar <= ratio[0] &&
         target + 20 >= ratio[0] && !clipped ) {
        return 0;
    }
    miss = target - ratio[0];
    if ( abs(miss) >= mar * 4 ) {
        return 2;
    }
    if ( miss < -20 ) {
        miss = -20;
    }
    if ( miss > mar ) {
        miss = mar;
    }
    ratio[0] = target - miss;
    return 1;
}

void
canon_600_auto_wb() {
    int mar;
    int row;
    int col;
    int i;
    int j;
    int st;
    int count[] = {0, 0};
    int test[8];
    int total[2][8];
    int ratio[2][2];
    int stat[2];

    memset(&total, 0, sizeof total);
    i = (int)lround(canon_ev + 0.5);
    if ( i < 10 ) {
        mar = 150;
    } else {
        if ( i > 12 ) {
            mar = 20;
        } else {
            mar = 280 - 20 * i;
        }
    }
    if ( flash_used ) {
        mar = 80;
    }
    for ( row = 14; row < THE_image.height - 14; row += 4 ) {
        for ( col = 10; col < THE_image.width; col += 2 ) {
            for ( i = 0; i < 8; i++ ) {
                test[(i & 4) + FC(row + (i >> 1), col + (i & 1))] =
                        BAYER(row + (i >> 1), col + (i & 1));
            }
            for ( i = 0; i < 8; i++ ) {
                if ( test[i] < 150 || test[i] > 1500 ) {
                    goto next;
                }
            }
            for ( i = 0; i < 4; i++ ) {
                if ( abs(test[i] - test[i + 4]) > 50 ) {
                    goto next;
                }
            }
            for ( i = 0; i < 2; i++ ) {
                for ( j = 0; j < 4; j += 2 ) {
                    ratio[i][j >> 1] = ((test[i * 4 + j + 1] - test[i * 4 + j]) << 10) / test[i * 4 + j];
                }
                stat[i] = canon_600_color(ratio[i], mar);
            }
            if ( (st = stat[0] | stat[1]) > 1 ) {
                goto next;
            }
            for ( i = 0; i < 2; i++ ) {
                if ( stat[i] ) {
                    for ( j = 0; j < 2; j++ ) {
                        test[i * 4 + j * 2 + 1] = test[i * 4 + j * 2] * (0x400 + ratio[i][j]) >> 10;
                    }
                }
            }
            for ( i = 0; i < 8; i++ ) {
                total[st][i] += test[i];
            }
            count[st]++;
            next:;
        }
    }
    if ( count[0] | count[1] ) {
        st = count[0] * 200 < count[1];
        for ( i = 0; i < 4; i++ ) {
            pre_mul[i] = 1.0 / (total[st][i] + total[st][i + 4]);
        }
    }
}

void
canon_600_coeff() {
    static const short table[6][12] = {
            {-190,  702,  -1878, 2390, 1861, -1349, 905, -393, -432,  944,  2617, -2105},
            {-1203, 1715, -1136, 1648, 1388, -876,  267, 245,  -1641, 2153, 3921, -3409},
            {-615,  1127, -1563, 2075, 1437, -925,  509, 3,    -756,  1268, 2519, -2007},
            {-190,  702,  -1886, 2398, 2153, -1641, 763, -251, -452,  964,  3040, -2528},
            {-190,  702,  -1878, 2390, 1861, -1349, 905, -393, -432,  944,  2617, -2105},
            {-807,  1319, -1785, 2297, 1388, -876,  769, -257, -230,  742,  2067, -1555}};
    int t = 0;
    int i;
    int c;
    float mc;
    float yc;

    mc = pre_mul[1] / pre_mul[2];
    yc = pre_mul[3] / pre_mul[2];
    if ( mc > 1 && mc <= 1.28 && yc < 0.8789 ) t = 1;
    if ( mc > 1.28 && mc <= 2 ) {
        if ( yc < 0.8789 ) {
            t = 3;
        } else if ( yc <= 2 ) {
                t = 4;
            }
    }
    if ( flash_used ) {
        t = 5;
    }
    for ( GLOBAL_colorTransformForRaw = i = 0; i < 3; i++ ) {
        for ( c = 0; c < IMAGE_colors; c++ ) {
            rgb_cam[i][c] = table[t][i * 4 + c] / 1024.0;
        }
    }
}

void
canon_600_load_raw() {
    unsigned char data[1120], *dp;
    unsigned short *pix;
    int irow;
    int row;

    for ( irow = row = 0; irow < THE_image.height; irow++ ) {
        if ( fread(data, 1, 1120, GLOBAL_IO_ifp) < 1120 ) inputOutputError();
        pix = THE_image.rawData + row * THE_image.width;
        for ( dp = data; dp < data + 1120; dp += 10, pix += 8 ) {
            pix[0] = (dp[0] << 2) + (dp[1] >> 6);
            pix[1] = (dp[2] << 2) + (dp[1] >> 4 & 3);
            pix[2] = (dp[3] << 2) + (dp[1] >> 2 & 3);
            pix[3] = (dp[4] << 2) + (dp[1] & 3);
            pix[4] = (dp[5] << 2) + (dp[9] & 3);
            pix[5] = (dp[6] << 2) + (dp[9] >> 2 & 3);
            pix[6] = (dp[7] << 2) + (dp[9] >> 4 & 3);
            pix[7] = (dp[8] << 2) + (dp[9] >> 6);
        }
        if ( (row += 2) > THE_image.height ) {
            row = 1;
        }
    }
}

void
canon_600_correct() {
    int row;
    int col;
    int val;
    static const short mul[4][2] =
            {{1141, 1145},
             {1128, 1109},
             {1178, 1149},
             {1128, 1109}};

    for ( row = 0; row < THE_image.height; row++ )
        for ( col = 0; col < THE_image.width; col++ ) {
            if ((val = BAYER(row, col) - ADOBE_black) < 0 ) {
                val = 0;
            }
            val = val * mul[row & 3][col & 1] >> 9;
            BAYER(row, col) = val;
        }
    canon_600_fixed_wb(1311);
    canon_600_auto_wb();
    canon_600_coeff();
    ADOBE_maximum = (0x3ff - ADOBE_black) * 1109 >> 9;
    ADOBE_black = 0;
}

int
canon_s2is() {
    unsigned row;

    for ( row = 0; row < 100; row++ ) {
        fseek(GLOBAL_IO_ifp, row * 3340 + 3284, SEEK_SET);
        if ( getc(GLOBAL_IO_ifp) > 15 ) {
            return 1;
        }
    }
    return 0;
}

void
canon_load_raw() {
    unsigned short *pixel;
    unsigned short *prow;
    unsigned short *huff[2];
    int nblocks;
    int lowbits;
    int i;
    int c;
    int row;
    int r;
    int save;
    int val;
    int block;
    int diffbuf[64];
    int leaf;
    int len;
    int diff;
    int carry = 0;
    int pnum = 0;
    int base[2];

    crw_init_tables(tiff_compress, huff);
    lowbits = canonHasLowBits();
    if ( !lowbits ) ADOBE_maximum = 0x3ff;
    fseek(GLOBAL_IO_ifp, 540 + lowbits * THE_image.height * THE_image.width / 4, SEEK_SET);
    GLOBAL_IO_zeroAfterFf = 1;
    getbits(-1);
    for ( row = 0; row < THE_image.height; row += 8 ) {
        pixel = THE_image.rawData + row * THE_image.width;
        nblocks = MIN(8, THE_image.height - row) * THE_image.width >> 6;
        for ( block = 0; block < nblocks; block++ ) {
            memset(diffbuf, 0, sizeof diffbuf);
            for ( i = 0; i < 64; i++ ) {
                leaf = gethuff(huff[i > 0]);
                if ( leaf == 0 && i ) {
                    break;
                }
                if ( leaf == 0xff ) {
                    continue;
                }
                i += leaf >> 4;
                len = leaf & 15;
                if ( len == 0 ) {
                    continue;
                }
                diff = getbits(len);
                if ( (diff & (1 << (len - 1))) == 0 ) {
                    diff -= (1 << len) - 1;
                }
                if ( i < 64 ) {
                    diffbuf[i] = diff;
                }
            }
            diffbuf[0] += carry;
            carry = diffbuf[0];
            for ( i = 0; i < 64; i++ ) {
                if ( pnum++ % THE_image.width == 0 ) {
                    base[0] = base[1] = 512;
                }
                if ( (pixel[(block << 6) + i] = base[i & 1] += diffbuf[i]) >> 10 ) {
                    inputOutputError();
                }
            }
        }
        if ( lowbits ) {
            save = ftell(GLOBAL_IO_ifp);
            fseek(GLOBAL_IO_ifp, 26 + row * THE_image.width / 4, SEEK_SET);
            for ( prow = pixel, i = 0; i < THE_image.width * 2; i++ ) {
                c = fgetc(GLOBAL_IO_ifp);
                for ( r = 0; r < 8; r += 2, prow++ ) {
                    val = (*prow << 2) + ((c >> r) & 3);
                    if ( THE_image.width == 2672 && val < 512 ) {
                        val += 2;
                    }
                    *prow = val;
                }
            }
            fseek(GLOBAL_IO_ifp, save, SEEK_SET);
        }
    }
    for ( c = 0; c < 2; c++ ) {
        free(huff[c]);
    }
}

void
canon_sraw_load_raw() {
    struct jhead jh;
    short *rp = 0;
    short (*ip)[4];
    int jwide;
    int slice;
    int scol;
    int ecol;
    int row;
    int col;
    int jrow = 0;
    int jcol = 0;
    int pix[3];
    int c;
    int v[3] = {0, 0, 0};
    int ver;
    int hue;
    char *cp;

    if ( !ljpeg_start(&jh, 0) || jh.clrs < 4 ) {
        return;
    }
    jwide = (jh.wide >>= 1) * jh.clrs;

    for ( ecol = slice = 0; slice <= cr2_slice[0]; slice++ ) {
        scol = ecol;
        ecol += cr2_slice[1] * 2 / jh.clrs;
        if ( !cr2_slice[0] || ecol > THE_image.width - 1 ) {
            ecol = THE_image.width & -2;
        }
        for ( row = 0; row < height; row += (jh.clrs >> 1) - 1 ) {
            ip = (short (*)[4]) GLOBAL_image + row * width;
            for ( col = scol; col < ecol; col += 2, jcol += jh.clrs ) {
                if ((jcol %= jwide) == 0 ) {
                    rp = (short *) ljpeg_row(jrow++, &jh);
                }
                if ( col >= width ) {
                    continue;
                }
                for ( c = 0; c < jh.clrs - 2; c++ ) {
                    ip[col + (c >> 1) * width + (c & 1)][0] = rp[jcol + c];
                }
                ip[col][1] = rp[jcol + jh.clrs - 2] - 16384;
                ip[col][2] = rp[jcol + jh.clrs - 1] - 16384;
            }
        }
    }
    for ( cp = model2; *cp && !isdigit(*cp); cp++ );
    sscanf(cp, "%d.%d.%d", v, v + 1, v + 2);
    ver = (v[0] * 1000 + v[1]) * 1000 + v[2];
    hue = (jh.sraw + 1) << 2;
    if ( unique_id >= 0x80000281 || (unique_id == 0x80000218 && ver > 1000006) ) {
        hue = jh.sraw << 1;
    }
    ip = (short (*)[4]) GLOBAL_image;
    rp = ip[0];
    for ( row = 0; row < height; row++, ip += width ) {
        if ( row & (jh.sraw >> 1))
            for ( col = 0; col < width; col += 2 )
                for ( c = 1; c < 3; c++ ) {
                    if ( row == height - 1 ) {
                        ip[col][c] = ip[col - width][c];
                    } else {
                        ip[col][c] = (ip[col - width][c] + ip[col + width][c] + 1) >> 1;
                    }
                }
        for ( col = 1; col < width; col += 2 ) {
            for ( c = 1; c < 3; c++ ) {
                if ( col == width - 1 ) {
                    ip[col][c] = ip[col - 1][c];
                } else {
                    ip[col][c] = (ip[col - 1][c] + ip[col + 1][c] + 1) >> 1;
                }
            }
        }
    }
    for ( ; rp < ip[0]; rp += 4 ) {
        if ( unique_id == 0x80000218 ||
             unique_id == 0x80000250 ||
             unique_id == 0x80000261 ||
             unique_id == 0x80000281 ||
             unique_id == 0x80000287 ) {
            rp[1] = (rp[1] << 2) + hue;
            rp[2] = (rp[2] << 2) + hue;
            pix[0] = rp[0] + ((50 * rp[1] + 22929 * rp[2]) >> 14);
            pix[1] = rp[0] + ((-5640 * rp[1] - 11751 * rp[2]) >> 14);
            pix[2] = rp[0] + ((29040 * rp[1] - 101 * rp[2]) >> 14);
        } else {
            if ( unique_id < 0x80000218 ) rp[0] -= 512;
            pix[0] = rp[0] + rp[2];
            pix[2] = rp[0] + rp[1];
            pix[1] = rp[0] + ((-778 * rp[1] - (rp[2] << 11)) >> 12);
        }
        for ( c = 0; c < 3; c++ ) {
            rp[c] = (short) CLIP(pix[c] * sraw_mul[c] >> 10);
        }
    }
    ljpeg_end(&jh);
    ADOBE_maximum = 0x3fff;
}

void
canon_rmf_load_raw() {
    int row;
    int col;
    int bits;
    int orow;
    int ocol;
    int c;

    for ( row = 0; row < THE_image.height; row++ ) {
        for ( col = 0; col < THE_image.width - 2; col += 3 ) {
            bits = read4bytes();
            for ( c = 0; c < 3; c++ ) {
                orow = row;
                if ((ocol = col + c - 4) < 0 ) {
                    ocol += THE_image.width;
                    if ((orow -= 2) < 0 ) {
                        orow += THE_image.height;
                    }
                }
                RAW(orow, ocol) = GAMMA_curveFunctionLookupTable[bits >> (10 * c + 2) & 0x3ff];
            }
        }
    }
    ADOBE_maximum = GAMMA_curveFunctionLookupTable[0x3ff];
}

void
canon_crx_load_raw() {
    fprintf(stderr, "canon_crx_load_raw() not implemented");
}
