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
