#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <csetjmp>
#include <jpeglib.h> // Decode compressed Kodak DC120 photos

#include "../../../common/globals.h"
#include "../../../common/mathMacros.h"
#include "../../../common/util.h"
#include "../../../common/CameraImageInformation.h"
#include "../../../imageHandling/BayessianImage.h"
#include "../../../colorRepresentation/adobeCoeff.h"
#include "../../../postprocessors/gamma.h"
#include "../globalsio.h"
#include "jpegRawLoaders.h"
#include "kodakRawLoaders.h"

void
kodak_c330_load_raw() {
    unsigned char *pixel;
    int row;
    int col;
    int y;
    int cb;
    int cr;
    int rgb[3];
    int c;

    pixel = (unsigned char *)calloc(THE_image.width, 2 * sizeof *pixel);
    memoryError(pixel, "kodak_c330_load_raw()");
    for ( row = 0; row < height; row++ ) {
        if ( fread(pixel, THE_image.width, 2, GLOBAL_IO_ifp) < 2 ) {
            inputOutputError();
        }
        if ( GLOBAL_loadFlags && (row & 31) == 31 ) {
            fseek(GLOBAL_IO_ifp, THE_image.width * 32, SEEK_CUR);
        }
        for ( col = 0; col < width; col++ ) {
            y = pixel[col * 2];
            cb = pixel[(col * 2 & -4) | 1] - 128;
            cr = pixel[(col * 2 & -4) | 3] - 128;
            rgb[1] = y - ((cb + cr + 2) >> 2);
            rgb[2] = rgb[1] + cb;
            rgb[0] = rgb[1] + cr;
            for ( c = 0; c < 3; c++ ) {
                GLOBAL_image[row * width + col][c] = GAMMA_curveFunctionLookupTable[LIM(rgb[c], 0, 255)];
            };
        }
    }
    free(pixel);
    ADOBE_maximum = GAMMA_curveFunctionLookupTable[0xff];
}

void
kodak_c603_load_raw() {
    unsigned char *pixel;
    int row;
    int col;
    int y;
    int cb;
    int cr;
    int rgb[3];
    int c;

    pixel = (unsigned char *)calloc(THE_image.width, 3 * sizeof *pixel);
    memoryError(pixel, "kodak_c603_load_raw()");
    for ( row = 0; row < height; row++ ) {
        if ( ~row & 1 ) {
            if ( fread(pixel, THE_image.width, 3, GLOBAL_IO_ifp) < 3 ) {
                inputOutputError();
            }
        }
        for ( col = 0; col < width; col++ ) {
            y = pixel[width * 2 * (row & 1) + col];
            cb = pixel[width + (col & -2)] - 128;
            cr = pixel[width + (col & -2) + 1] - 128;
            rgb[1] = y - ((cb + cr + 2) >> 2);
            rgb[2] = rgb[1] + cb;
            rgb[0] = rgb[1] + cr;
            for ( c = 0; c < 3; c++ ) {
                GLOBAL_image[row * width + col][c] = GAMMA_curveFunctionLookupTable[LIM(rgb[c], 0, 255)];
            };
        }
    }
    free(pixel);
    ADOBE_maximum = GAMMA_curveFunctionLookupTable[0xff];
}

void
kodak_262_load_raw() {
    static const unsigned char kodak_tree[2][26] =
            {{0, 1, 5, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
             {0, 3, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9}};
    unsigned short *huff[2];
    unsigned char *pixel;
    int *strip;
    int ns;
    int c;
    int row;
    int col;
    int chess;
    int pi = 0;
    int pi1;
    int pi2;
    int pred;
    int val;

    for ( c = 0; c < 2; c++ ) {
        huff[c] = make_decoder(kodak_tree[c]);
    }
    ns = (THE_image.height + 63) >> 5;
    pixel = (unsigned char *) malloc(THE_image.width * 32 + ns * 4);
    memoryError(pixel, "kodak_262_load_raw()");
    strip = (int *) (pixel + THE_image.width * 32);
    GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
    for ( c = 0; c < ns; c++ ) {
        strip[c] = read4bytes();
    }
    for ( row = 0; row < THE_image.height; row++ ) {
        if ((row & 31) == 0 ) {
            fseek(GLOBAL_IO_ifp, strip[row >> 5], SEEK_SET);
            getbits(-1);
            pi = 0;
        }
        for ( col = 0; col < THE_image.width; col++ ) {
            chess = (row + col) & 1;
            pi1 = chess ? pi - 2 : pi - THE_image.width - 1;
            pi2 = chess ? pi - 2 * THE_image.width : pi - THE_image.width + 1;
            if ( col <= chess ) {
                pi1 = -1;
            }
            if ( pi1 < 0 ) {
                pi1 = pi2;
            }
            if ( pi2 < 0 ) {
                pi2 = pi1;
            }
            if ( pi1 < 0 && col > 1 ) {
                pi1 = pi2 = pi - 2;
            }
            pred = (pi1 < 0) ? 0 : (pixel[pi1] + pixel[pi2]) >> 1;
            pixel[pi] = val = pred + ljpeg_diff(huff[chess]);
            if ( val >> 8 ) {
                inputOutputError();
            }
            val = GAMMA_curveFunctionLookupTable[pixel[pi++]];
            RAW(row, col) = val;
        }
    }
    free(pixel);
    for ( c = 0; c < 2; c++ ) {
        free(huff[c]);
    }
}

static int
kodak_65000_decode(short *out, int bsize) {
    unsigned char c;
    unsigned char blen[768];
    unsigned short raw[6];
    long long bitbuf = 0;
    int save;
    int bits = 0;
    int i;
    int j;
    int len;
    int diff;

    save = ftell(GLOBAL_IO_ifp);
    bsize = (bsize + 3) & -4;
    for ( i = 0; i < bsize; i += 2 ) {
        c = fgetc(GLOBAL_IO_ifp);
        if ( (blen[i] = c & 15) > 12 ||
             (blen[i + 1] = c >> 4) > 12 ) {
            fseek(GLOBAL_IO_ifp, save, SEEK_SET);
            for ( i = 0; i < bsize; i += 8 ) {
                readShorts(raw, 6);
                out[i] = raw[0] >> 12 << 8 | raw[2] >> 12 << 4 | raw[4] >> 12;
                out[i + 1] = raw[1] >> 12 << 8 | raw[3] >> 12 << 4 | raw[5] >> 12;
                for ( j = 0; j < 6; j++ ) {
                    out[i + 2 + j] = raw[j] & 0xfff;
                }
            }
            return 1;
        }
    }
    if ( (bsize & 7) == 4 ) {
        bitbuf = fgetc(GLOBAL_IO_ifp) << 8;
        bitbuf += fgetc(GLOBAL_IO_ifp);
        bits = 16;
    }
    for ( i = 0; i < bsize; i++ ) {
        len = blen[i];
        if ( bits < len ) {
            for ( j = 0; j < 32; j += 8 ) {
                bitbuf += (long long) fgetc(GLOBAL_IO_ifp) << (bits + (j ^ 8));
            }
            bits += 32;
        }
        diff = bitbuf & (0xffff >> (16 - len));
        bitbuf >>= len;
        bits -= len;
        if ( (diff & (1 << (len - 1))) == 0 ) {
            diff -= (1 << len) - 1;
        }
        out[i] = diff;
    }
    return 0;
}

void
kodak_65000_load_raw() {
    short buf[256];
    int row;
    int col;
    int len;
    int pred[2];
    int ret;
    int i;

    for ( row = 0; row < height; row++ ) {
        for ( col = 0; col < width; col += 256 ) {
            pred[0] = pred[1] = 0;
            len = MIN (256, width - col);
            ret = kodak_65000_decode(buf, len);
            for ( i = 0; i < len; i++ ) {
                if ( (RAW(row, col + i) = GAMMA_curveFunctionLookupTable[ret ? buf[i] : (pred[i & 1] += buf[i])]) >> 12 ) {
                    inputOutputError();
                }
            }
        }
    }
}

void
kodak_ycbcr_load_raw() {
    short buf[384];
    short *bp;
    int row;
    int col;
    int len;
    int c;
    int i;
    int j;
    int k;
    int y[2][2];
    int cb;
    int cr;
    int rgb[3];
    unsigned short *ip;

    if ( !GLOBAL_image ) {
        return;
    }
    for ( row = 0; row < height; row += 2 ) {
        for ( col = 0; col < width; col += 128 ) {
            len = MIN (128, width - col);
            kodak_65000_decode(buf, len * 3);
            y[0][1] = y[1][1] = cb = cr = 0;
            for ( bp = buf, i = 0; i < len; i += 2, bp += 2 ) {
                cb += bp[4];
                cr += bp[5];
                rgb[1] = -((cb + cr + 2) >> 2);
                rgb[2] = rgb[1] + cb;
                rgb[0] = rgb[1] + cr;
                for ( j = 0; j < 2; j++ )
                    for ( k = 0; k < 2; k++ ) {
                        if ((y[j][k] = y[j][k ^ 1] + *bp++) >> 10 ) inputOutputError();
                        ip = GLOBAL_image[(row + j) * width + col + i + k];
                        for ( c = 0; c < 3; c++ ) {
                            ip[c] = GAMMA_curveFunctionLookupTable[LIM(y[j][k] + rgb[c], 0, 0xfff)];
                        }
                    }
            }
        }
    }
}

void
kodak_rgb_load_raw() {
    short buf[768];
    short *bp;
    int row;
    int col;
    int len;
    int c;
    int i;
    int rgb[3];
    unsigned short *ip = GLOBAL_image[0];

    for ( row = 0; row < height; row++ ) {
        for ( col = 0; col < width; col += 256 ) {
            len = MIN (256, width - col);
            kodak_65000_decode(buf, len * 3);
            memset(rgb, 0, sizeof rgb);
            for ( bp = buf, i = 0; i < len; i++, ip += 4 ) {
                for ( c = 0; c < 3; c++ ) {
                    if ((ip[c] = rgb[c] += *bp++) >> 12 ) inputOutputError();
                }
            }
        }
    }
}

void
kodak_thumb_load_raw() {
    int row;
    int col;

    IMAGE_colors = thumb_misc >> 5;
    for ( row = 0; row < height; row++ ) {
        for ( col = 0; col < width; col++ ) {
            readShorts(GLOBAL_image[row * width + col], IMAGE_colors);
        }
    }
    ADOBE_maximum = (1 << (thumb_misc & 31)) - 1;
}

METHODDEF(boolean)
fill_input_buffer(j_decompress_ptr cinfo) {
    static unsigned char jpeg_buffer[4096];
    size_t nbytes;

    nbytes = fread(jpeg_buffer, 1, 4096, GLOBAL_IO_ifp);
    swab(jpeg_buffer, jpeg_buffer, nbytes);
    cinfo->src->next_input_byte = jpeg_buffer;
    cinfo->src->bytes_in_buffer = nbytes;
    return TRUE;
}

void
kodak_jpeg_load_raw() {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPARRAY buf;
    JSAMPLE (*pixel)[3];
    int row;
    int col;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress (&cinfo);
    jpeg_stdio_src(&cinfo, GLOBAL_IO_ifp);
    cinfo.src->fill_input_buffer = fill_input_buffer;
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);
    if ( (cinfo.output_width != width) ||
         (cinfo.output_height * 2 != height) ||
         (cinfo.output_components != 3) ) {
        fprintf(stderr, _("%s: incorrect JPEG dimensions\n"), CAMERA_IMAGE_information.inputFilename);
        jpeg_destroy_decompress(&cinfo);
        longjmp(failure, 3);
    }
    buf = (*cinfo.mem->alloc_sarray)
            ((j_common_ptr) &cinfo, JPOOL_IMAGE, width * 3, 1);

    while ( cinfo.output_scanline < cinfo.output_height ) {
        row = cinfo.output_scanline * 2;
        jpeg_read_scanlines(&cinfo, buf, 1);
        pixel = (JSAMPLE (*)[3]) buf[0];
        for ( col = 0; col < width; col += 2 ) {
            RAW(row + 0, col + 0) = pixel[col + 0][1] << 1;
            RAW(row + 1, col + 1) = pixel[col + 1][1] << 1;
            RAW(row + 0, col + 1) = pixel[col][0] + pixel[col + 1][0];
            RAW(row + 1, col + 0) = pixel[col][2] + pixel[col + 1][2];
        }
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    ADOBE_maximum = 0xff << 1;
}

void
kodak_dc120_load_raw() {
    static const int mul[4] = {162, 192, 187, 92};
    static const int add[4] = {0, 636, 424, 212};
    unsigned char pixel[848];
    int row;
    int shift;
    int col;

    for ( row = 0; row < height; row++ ) {
        if ( fread(pixel, 1, 848, GLOBAL_IO_ifp) < 848 ) inputOutputError();
        shift = row * mul[row & 3] + add[row & 3];
        for ( col = 0; col < width; col++ ) {
            RAW(row, col) = (unsigned short) pixel[(col + shift) % 848];
        }
    }
    ADOBE_maximum = 0xff;
}

#define radc_token(tree) ((signed char) getbithuff(8,huff[tree]))

#define FORYX for (y=1; y < 3; y++) for (x=col+1; x >= col; x--)

#define PREDICTOR (c ? (buf[c][y-1][x] + buf[c][y][x+1]) / 2 \
: (buf[c][y-1][x+1] + 2*buf[c][y-1][x] + buf[c][y][x+1]) / 4)

void
kodak_radc_load_raw() {
    static const char src[] = {
            1, 1, 2, 3, 3, 4, 4, 2, 5, 7, 6, 5, 7, 6, 7, 8,
            1, 0, 2, 1, 3, 3, 4, 4, 5, 2, 6, 7, 7, 6, 8, 5, 8, 8,
            2, 1, 2, 3, 3, 0, 3, 2, 3, 4, 4, 6, 5, 5, 6, 7, 6, 8,
            2, 0, 2, 1, 2, 3, 3, 2, 4, 4, 5, 6, 6, 7, 7, 5, 7, 8,
            2, 1, 2, 4, 3, 0, 3, 2, 3, 3, 4, 7, 5, 5, 6, 6, 6, 8,
            2, 3, 3, 1, 3, 2, 3, 4, 3, 5, 3, 6, 4, 7, 5, 0, 5, 8,
            2, 3, 2, 6, 3, 0, 3, 1, 4, 4, 4, 5, 4, 7, 5, 2, 5, 8,
            2, 4, 2, 7, 3, 3, 3, 6, 4, 1, 4, 2, 4, 5, 5, 0, 5, 8,
            2, 6, 3, 1, 3, 3, 3, 5, 3, 7, 3, 8, 4, 0, 5, 2, 5, 4,
            2, 0, 2, 1, 3, 2, 3, 3, 4, 4, 4, 5, 5, 6, 5, 7, 4, 8,
            1, 0, 2, 2, 2, -2,
            1, -3, 1, 3,
            2, -17, 2, -5, 2, 5, 2, 17,
            2, -7, 2, 2, 2, 9, 2, 18,
            2, -18, 2, -9, 2, -2, 2, 7,
            2, -28, 2, 28, 3, -49, 3, -9, 3, 9, 4, 49, 5, -79, 5, 79,
            2, -1, 2, 13, 2, 26, 3, 39, 4, -16, 5, 55, 6, -37, 6, 76,
            2, -26, 2, -13, 2, 1, 3, -39, 4, 16, 5, -55, 6, -76, 6, 37
    };
    unsigned short huff[19][256];
    int row;
    int col;
    int tree;
    int nreps;
    int rep;
    int step;
    int i;
    int c;
    int s;
    int r;
    int x;
    int y;
    int val;
    short last[3] = {16, 16, 16}, mul[3];
    short buf[3][3][386];
    static const unsigned short pt[] =
            {0, 0, 1280, 1344, 2320, 3616, 3328, 8000, 4095, 16383, 65535, 16383};

    for ( i = 2; i < 12; i += 2 ) {
        for ( c = pt[i - 2]; c <= pt[i]; c++ ) {
            GAMMA_curveFunctionLookupTable[c] = (float)
                                                        (c - pt[i - 2]) / (pt[i] - pt[i - 2]) * (pt[i + 1] - pt[i - 1]) + pt[i - 1] + 0.5;
        }
    }
    for ( s = i = 0; i < sizeof src; i += 2 ) {
        for ( c = 0; c < 256 >> src[i]; c++ ) {
            ((unsigned short *)huff)[s++] = src[i] << 8 | (unsigned char)src[i + 1];
        }
    }
    s = kodak_cbpp == 243 ? 2 : 3;
    for ( c = 0; c < 256; c++ ) {
        huff[18][c] = (8 - s) << 8 | c >> s << s | 1 << (s - 1);
    }
    getbits(-1);
    for ( i = 0; i < sizeof(buf) / sizeof(short); i++ ) {
        ((short *) buf)[i] = 2048;
    }
    for ( row = 0; row < height; row += 4 ) {
        for ( c = 0; c < 3; c++ ) {
            mul[c] = getbits(6);
        }
        for ( c = 0; c < 3; c++ ) {
            val = ((0x1000000 / last[c] + 0x7ff) >> 12) * mul[c];
            s = val > 65564 ? 10 : 12;
            x = ~(-1 << (s - 1));
            val <<= 12 - s;
            for ( i = 0; i < sizeof(buf[0]) / sizeof(short); i++ ) {
                ((short *) buf[c])[i] = (((short *) buf[c])[i] * val + x) >> s;
            }
            last[c] = mul[c];
            for ( r = 0; r <= !c; r++ ) {
                buf[c][1][width / 2] = buf[c][2][width / 2] = mul[c] << 7;
                for ( tree = 1, col = width / 2; col > 0; ) {
                    if ((tree = radc_token(tree))) {
                        col -= 2;
                        if ( tree == 8 ) {
                            FORYX buf[c][y][x] = (unsigned char) radc_token(18) * mul[c];
                        } else {
                            FORYX buf[c][y][x] = radc_token(tree + 10) * 16 + PREDICTOR;
                        }
                    } else {
                        do {
                            nreps = (col > 2) ? radc_token(9) + 1 : 1;
                            for ( rep = 0; rep < 8 && rep < nreps && col > 0; rep++ ) {
                                col -= 2;
                                FORYX buf[c][y][x] = PREDICTOR;
                                if ( rep & 1 ) {
                                    step = radc_token(10) << 4;
                                    FORYX buf[c][y][x] += step;
                                }
                            }
                        }
                        while ( nreps == 9 );
                    }
                }
                for ( y = 0; y < 2; y++ ) {
                    for ( x = 0; x < width / 2; x++ ) {
                        val = (buf[c][y + 1][x] << 4) / mul[c];
                        if ( val < 0 ) {
                            val = 0;
                        }
                        if ( c ) {
                            RAW(row + y * 2 + c - 1, x * 2 + 2 - c) = val;
                        } else {
                            RAW(row + r * 2 + y, x * 2 + y) = val;
                        }
                    }
                }
                memcpy(buf[c][0] + !c, buf[c][2], sizeof buf[c][0] - 2 * !c);
            }
        }
        for ( y = row; y < row + 4; y++ ) {
            for ( x = 0; x < width; x++ ) {
                if ((x + y) & 1 ) {
                    r = x ? x - 1 : x + 1;
                    s = x + 1 < width ? x + 1 : x - 1;
                    val = (RAW(y, x) - 2048) * 2 + (RAW(y, r) + RAW(y, s)) / 2;
                    if ( val < 0 ) val = 0;
                    RAW(y, x) = val;
                }
            }
        }
    }
    for ( i = 0; i < height * width; i++ ) {
        THE_image.rawData[i] = GAMMA_curveFunctionLookupTable[THE_image.rawData[i]];
    }
    ADOBE_maximum = 0x3fff;
}
