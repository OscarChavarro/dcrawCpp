/*
   dcraw.c -- Dave Coffin's raw photo decoder
   Copyright 1997-2018 by Dave Coffin, dcoffin a cybercom o net

   This is a command-line ANSI C program to convert raw photos from
   any digital camera on any computer running any operating system.

   No license is required to download and use dcraw.c.  However,
   to lawfully redistribute dcraw, you must either (a) offer, at
   no extra charge, full source code* for all executable files
   containing RESTRICTED functions, (b) distribute this code under
   the GPL Version 2 or later, (c) remove all RESTRICTED functions,
   re-implement them, or copy them from an earlier, unrestricted
   Revision of dcraw.c, or (d) purchase a license from the author.

   The functions that process Foveon images have been RESTRICTED
   since Revision 1.237.  All other code remains free for all uses.

   *If you have not modified dcraw.c in any way, a link to my
   homepage qualifies as "full source code".

   $Revision: 1.478 $
   $Date: 2018/06/01 20:36:25 $
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(DJGPP) || defined(__MINGW32__)
                                                                                                                        #define fseeko fseek
#define ftello ftell
#else
#define fgetc getc_unlocked
#endif
#ifdef __CYGWIN__
#include <io.h>
#endif
#ifdef WIN32
                                                                                                                        #include <sys/utime.h>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#define snprintf _snprintf
#define strcasecmp stricmp
#define strncasecmp strnicmp
typedef __int64 INT64;
typedef unsigned __int64 UINT64;
#else

#include <unistd.h>
#include <utime.h>

typedef long long INT64;
typedef unsigned long long UINT64;
#endif

#ifdef NODEPS
                                                                                                                        #define NO_JASPER
#define NO_JPEG
#define NO_LCMS
#endif

// TODO: Enable this again! being turned off temporary
#define NO_JASPER

#ifndef NO_JASPER
#include <jasper/jasper.h>	/* Decode Red camera movies */
#endif
#ifndef NO_JPEG

#include <jpeglib.h>        /* Decode compressed Kodak DC120 photos */

#endif                /* and Adobe Lossy DNGs */
#ifndef NO_LCMS

// External libraries
#include <lcms2.h>        /* Support color profiles */
#include <tiff.h>

#endif

// App modules
#include "common/globals.h"
#include "common/mathMacros.h"
#include "common/Options.h"
#include "imageHandling/rawAnalysis.h"
#include "persistence/readers/globalsio.h"
#include "common/CameraImageInformation.h"
#include "common/util.h"
#include "colorRepresentation/adobeCoeff.h"
#include "postprocessors/gamma.h"
#include "persistence/readers/rawloaders/jpegRawLoaders.h"
#include "persistence/readers/rawloaders/sonyRawLoaders.h"
#include "persistence/readers/rawloaders/nikonRawLoaders.h"
#include "persistence/readers/rawloaders/hasselbladRawLoaders.h"
#include "persistence/readers/rawloaders/canonRawLoaders.h"
#include "persistence/readers/rawloaders/standardRawLoaders.h"
#include "persistence/readers/rawloaders/samsungRawLoaders.h"
#include "persistence/readers/rawloaders/dngRawLoaders.h"
#include "persistence/readers/rawloaders/kodakRawLoaders.h"
#include "persistence/readers/rawloaders/pentaxRawLoaders.h"
#include "persistence/readers/rawloaders/rolleiRawLoaders.h"
#include "persistence/readers/rawloaders/phaseoneRawLoaders.h"
#include "persistence/readers/rawloaders/leafRawLoaders.h"
#include "persistence/readers/rawloaders/sinarRawLoaders.h"
#include "persistence/readers/rawloaders/imaconRawLoaders.h"
#include "persistence/readers/rawloaders/nokiaRawLoaders.h"
#include "persistence/readers/rawloaders/panasonicRawLoaders.h"
#include "persistence/readers/rawloaders/olympusRawLoaders.h"

char *meta_data;
char xtrans[6][6];
char xtrans_abs[6][6];
char desc[512];
char artist[64];
float iso_speed;
float aperture;
float focal_len;
time_t timestamp;
off_t thumb_offset;
off_t profile_offset;
unsigned shot_order;
unsigned exif_cfa;
unsigned profile_length;
unsigned fuji_layout;
unsigned numberOfRawImages;
unsigned zero_is_bad;
unsigned is_foveon;
unsigned gpsdata[32];
unsigned cameraFlip;
unsigned short fuji_width;
unsigned short white[8][8];
double pixel_aspect;
int mask[8][4];
float cmatrix[3][4];
const double xyz_rgb[3][3] = { // XYZ from RGB
        {0.412453, 0.357580, 0.180423},
        {0.212671, 0.715160, 0.072169},
        {0.019334, 0.119193, 0.950227}
};
const float d65_white[3] = {0.950456, 1, 1.088754};
int histogram[4][0x2000];

void (*write_thumb)(), (*write_fun)();

void (*TIFF_CALLBACK_loadRawData)();

struct decode {
    struct decode *branch[2];
    int leaf;
};
struct decode first_decode[2048];
struct decode *free_decode;

struct tiff_ifd {
    int width;
    int height;
    int bps;
    int comp;
    int phint;
    int offset;
    int flip;
    int samples;
    int bytes;
    int tile_width;
    int tile_length;
    float shutter;
};
struct tiff_ifd ifdArray[10];

int
fcol(int row, int col) {
    static const char filter[16][16] =
            {{2, 1, 1, 3, 2, 3, 2, 0, 3, 2, 3, 0, 1, 2, 1, 0},
             {0, 3, 0, 2, 0, 1, 3, 1, 0, 1, 1, 2, 0, 3, 3, 2},
             {2, 3, 3, 2, 3, 1, 1, 3, 3, 1, 2, 1, 2, 0, 0, 3},
             {0, 1, 0, 1, 0, 2, 0, 2, 2, 0, 3, 0, 1, 3, 2, 1},
             {3, 1, 1, 2, 0, 1, 0, 2, 1, 3, 1, 3, 0, 1, 3, 0},
             {2, 0, 0, 3, 3, 2, 3, 1, 2, 0, 2, 0, 3, 2, 2, 1},
             {2, 3, 3, 1, 2, 1, 2, 1, 2, 1, 1, 2, 3, 0, 0, 1},
             {1, 0, 0, 2, 3, 0, 0, 3, 0, 3, 0, 3, 2, 1, 2, 3},
             {2, 3, 3, 1, 1, 2, 1, 0, 3, 2, 3, 0, 2, 3, 1, 3},
             {1, 0, 2, 0, 3, 0, 3, 2, 0, 1, 1, 2, 0, 1, 0, 2},
             {0, 1, 1, 3, 3, 2, 2, 1, 1, 3, 3, 0, 2, 1, 3, 2},
             {2, 3, 2, 0, 0, 1, 3, 0, 2, 0, 1, 2, 3, 0, 1, 0},
             {1, 3, 1, 2, 3, 2, 3, 2, 0, 2, 0, 1, 1, 0, 3, 0},
             {0, 2, 0, 3, 1, 0, 0, 1, 1, 3, 3, 2, 3, 2, 2, 1},
             {2, 1, 3, 2, 3, 1, 2, 1, 0, 3, 0, 2, 0, 2, 0, 2},
             {0, 3, 1, 0, 0, 2, 0, 3, 2, 1, 3, 1, 1, 3, 1, 3}};

    if ( IMAGE_filters == 1 ) {
        return filter[(row + top_margin) & 15][(col + left_margin) & 15];
    }
    if ( IMAGE_filters == 9 ) {
        return xtrans[(row + 6) % 6][(col + 6) % 6];
    }
    return FC(row, col);
}

#ifndef __GLIBC__

char *
my_memmem(char *haystack, size_t haystacklen,
                char *needle, size_t needlelen) {
    char *c;
    for ( c = haystack; c <= haystack + haystacklen - needlelen; c++ ) {
        if ( !memcmp(c, needle, needlelen)) {
            return c;
        }
    }
    return 0;
}

#define memmem my_memmem

char *
my_strcasestr(char *haystack, const char *needle) {
    char *c;
    for ( c = haystack; *c; c++ ) {
        if ( !strncasecmp(c, needle, strlen(needle))) {
            return c;
        }
    }
    return 0;
}

#define strcasestr my_strcasestr
#endif

#define unsignedEndianSwap(s) unsignedEndianSwap((unsigned char *)s)

/*
Separates a Minolta DiMAGE Z2 from a Nikon E4300.
*/
int
minolta_z2() {
    int i;
    int nz;
    char tail[424];

    fseek(GLOBAL_IO_ifp, -sizeof tail, SEEK_END);
    fread(tail, 1, sizeof tail, GLOBAL_IO_ifp);
    for ( nz = i = 0; i < sizeof tail; i++ ) {
        if ( tail[i] ) {
            nz++;
        }
    }
    return nz > 20;
}

void
jpeg_thumb();

void
ppm_thumb() {
    char *thumb;

    thumb_length = thumb_width * thumb_height * 3;
    thumb = (char *) malloc(thumb_length);
    memoryError(thumb, "ppm_thumb()");
    fprintf(ofp, "P6\n%d %d\n255\n", thumb_width, thumb_height);
    fread(thumb, 1, thumb_length, GLOBAL_IO_ifp);
    fwrite(thumb, 1, thumb_length, ofp);
    free(thumb);
}

void
ppm16_thumb() {
    int i;
    char *thumb;

    thumb_length = thumb_width * thumb_height * 3;
    thumb = (char *) calloc(thumb_length, 2);
    memoryError(thumb, "ppm16_thumb()");
    readShorts((unsigned short *) thumb, thumb_length);
    for ( i = 0; i < thumb_length; i++ ) {
        thumb[i] = ((unsigned short *) thumb)[i] >> 8;
    }
    fprintf(ofp, "P6\n%d %d\n255\n", thumb_width, thumb_height);
    fwrite(thumb, 1, thumb_length, ofp);
    free(thumb);
}

void
layer_thumb() {
    int i;
    int c;
    char *thumb;
    char map[][4] = {"012", "102"};

    IMAGE_colors = thumb_misc >> 5 & 7;
    thumb_length = thumb_width * thumb_height;
    thumb = (char *) calloc(IMAGE_colors, thumb_length);
    memoryError(thumb, "layer_thumb()");
    fprintf(ofp, "P%d\n%d %d\n255\n",
            5 + (IMAGE_colors >> 1), thumb_width, thumb_height);
    fread(thumb, thumb_length, IMAGE_colors, GLOBAL_IO_ifp);
    for ( i = 0; i < thumb_length; i++ ) {
        for ( c = 0; c < IMAGE_colors; c++ ) {
            putc(thumb[i + thumb_length * (map[thumb_misc >> 8][c] - '0')], ofp);
        }
    }
    free(thumb);
}

void
fuji_xtrans_load_raw() {
    fprintf(stderr, "ERROR: Fiju xtrans not supported\n");
}

void
minolta_rd175_load_raw() {
    unsigned char pixel[768];
    unsigned irow;
    unsigned box;
    unsigned row;
    unsigned col;

    for ( irow = 0; irow < 1481; irow++ ) {
        if ( fread(pixel, 1, 768, GLOBAL_IO_ifp) < 768 ) {
            inputOutputError();
        }
        box = irow / 82;
        row = irow % 82 * 12 + ((box < 12) ? box | 1 : (box - 12) * 2);
        switch ( irow ) {
            case 1477:
            case 1479:
                continue;
            case 1476:
                row = 984;
                break;
            case 1480:
                row = 985;
                break;
            case 1478:
                row = 985;
                box = 1;
        }
        if ( (box < 12) && (box & 1) ) {
            for ( col = 0; col < 1533; col++, row ^= 1 ) {
                if ( col != 1 ) {
                    RAW(row, col) = (col + 1) & 2 ?
                                    pixel[col / 2 - 1] + pixel[col / 2 + 1] : pixel[col / 2] << 1;
                }
            }
            RAW(row, 1) = pixel[1] << 1;
            RAW(row, 1533) = pixel[765] << 1;
        } else {
            for ( col = row & 1; col < 1534; col += 2 ) {
                RAW(row, col) = pixel[col / 2] << 1;
            }
        }
    }
    ADOBE_maximum = 0xff << 1;
}

void
quicktake_100_load_raw() {
    unsigned char pixel[484][644];
    static const short gstep[16] =
            {-89, -60, -44, -32, -22, -15, -8, -2, 2, 8, 15, 22, 32, 44, 60, 89};
    static const short rstep[6][4] =
            {{-3,  -1, 1, 3},
             {-5,  -1, 1, 5},
             {-8,  -2, 2, 8},
             {-13, -3, 3, 13},
             {-19, -4, 4, 19},
             {-28, -6, 6, 28}};
    static const short curve[256] =
            {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
             28, 29, 30, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 53,
             54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 74, 75, 76, 77, 78,
             79, 80, 81, 82, 83, 84, 86, 88, 90, 92, 94, 97, 99, 101, 103, 105, 107, 110, 112, 114, 116,
             118, 120, 123, 125, 127, 129, 131, 134, 136, 138, 140, 142, 144, 147, 149, 151, 153, 155,
             158, 160, 162, 164, 166, 168, 171, 173, 175, 177, 179, 181, 184, 186, 188, 190, 192, 195,
             197, 199, 201, 203, 205, 208, 210, 212, 214, 216, 218, 221, 223, 226, 230, 235, 239, 244,
             248, 252, 257, 261, 265, 270, 274, 278, 283, 287, 291, 296, 300, 305, 309, 313, 318, 322,
             326, 331, 335, 339, 344, 348, 352, 357, 361, 365, 370, 374, 379, 383, 387, 392, 396, 400,
             405, 409, 413, 418, 422, 426, 431, 435, 440, 444, 448, 453, 457, 461, 466, 470, 474, 479,
             483, 487, 492, 496, 500, 508, 519, 531, 542, 553, 564, 575, 587, 598, 609, 620, 631, 643,
             654, 665, 676, 687, 698, 710, 721, 732, 743, 754, 766, 777, 788, 799, 810, 822, 833, 844,
             855, 866, 878, 889, 900, 911, 922, 933, 945, 956, 967, 978, 989, 1001, 1012, 1023};
    int rb;
    int row;
    int col;
    int sharp;
    int val = 0;

    getbits(-1);
    memset(pixel, 0x80, sizeof pixel);
    for ( row = 2; row < height + 2; row++ ) {
        for ( col = 2 + (row & 1); col < width + 2; col += 2 ) {
            val = ((pixel[row - 1][col - 1] + 2 * pixel[row - 1][col + 1] +
                    pixel[row][col - 2]) >> 2) + gstep[getbits(4)];
            pixel[row][col] = val = LIM(val, 0, 255);
            if ( col < 4 ) {
                pixel[row][col - 2] = pixel[row + 1][~row & 1] = val;
            }
            if ( row == 2 ) {
                pixel[row - 1][col + 1] = pixel[row - 1][col + 3] = val;
            }
        }
        pixel[row][col] = val;
    }
    for ( rb = 0; rb < 2; rb++ ) {
        for ( row = 2 + rb; row < height + 2; row += 2 ) {
            for ( col = 3 - (row & 1); col < width + 2; col += 2 ) {
                if ( row < 4 || col < 4 ) {
                    sharp = 2;
                } else {
                    val = ABS(pixel[row - 2][col] - pixel[row][col - 2])
                          + ABS(pixel[row - 2][col] - pixel[row - 2][col - 2])
                          + ABS(pixel[row][col - 2] - pixel[row - 2][col - 2]);
                    sharp = val < 4 ? 0 : val < 8 ? 1 : val < 16 ? 2 :
                                                        val < 32 ? 3 : val < 48 ? 4 : 5;
                }
                val = ((pixel[row - 2][col] + pixel[row][col - 2]) >> 1)
                      + rstep[sharp][getbits(2)];
                pixel[row][col] = val = LIM(val, 0, 255);
                if ( row < 4 ) {
                    pixel[row - 2][col + 2] = val;
                }
                if ( col < 4 ) {
                    pixel[row + 2][col - 2] = val;
                }
            }
        }
    }
    for ( row = 2; row < height + 2; row++ ) {
        for ( col = 3 - (row & 1); col < width + 2; col += 2 ) {
            val = ((pixel[row][col - 1] + (pixel[row][col] << 2) +
                    pixel[row][col + 1]) >> 1) - 0x100;
            pixel[row][col] = LIM(val, 0, 255);
        }
    }
    for ( row = 0; row < height; row++ ) {
        for ( col = 0; col < width; col++ ) {
            RAW(row, col) = curve[pixel[row + 2][col + 2]];
        }
    }
    ADOBE_maximum = 0x3ff;
}

#ifdef NO_JPEG
void kodak_jpeg_load_raw() {}
void lossy_dng_load_raw() {}
#else

void
gamma_curve(double pwr, double ts, int mode, int imax);

void
lossy_dng_load_raw() {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPARRAY buf;
    JSAMPLE (*pixel)[3];
    unsigned sorder = GLOBAL_endianOrder, ntags, opcode, deg, i, j, c;
    unsigned save = GLOBAL_IO_profileOffset - 4, trow = 0, tcol = 0, row, col;
    unsigned short cur[3][256];
    double coeff[9], tot;

    if ( GLOBAL_meta_offset ) {
        fseek(GLOBAL_IO_ifp, GLOBAL_meta_offset, SEEK_SET);
        GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
        ntags = read4bytes();
        while ( ntags-- ) {
            opcode = read4bytes();
            read4bytes();
            read4bytes();
            if ( opcode != 8 ) {
                fseek(GLOBAL_IO_ifp, read4bytes(), SEEK_CUR);
                continue;
            }
            fseek(GLOBAL_IO_ifp, 20, SEEK_CUR);
            if ((c = read4bytes()) > 2 ) {
                break;
            }
            fseek(GLOBAL_IO_ifp, 12, SEEK_CUR);
            if ((deg = read4bytes()) > 8 ) {
                break;
            }
            for ( i = 0; i <= deg && i < 9; i++ ) {
                coeff[i] = readDouble(12);
            }
            for ( i = 0; i < 256; i++ ) {
                for ( tot = j = 0; j <= deg; j++ ) {
                    tot += coeff[j] * pow(i / 255.0, j);
                }
                cur[c][i] = tot * 0xffff;
            }
        }
        GLOBAL_endianOrder = sorder;
    } else {
        gamma_curve(1 / 2.4, 12.92, 1, 255);
        for ( c = 0; c < 3; c++ ) {
            memcpy(cur[c], GAMMA_curveFunctionLookupTable, sizeof cur[0]);
        };
    }
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress (&cinfo);
    while ( trow < THE_image.height ) {
        fseek(GLOBAL_IO_ifp, save += 4, SEEK_SET);
        if ( tile_length < INT_MAX ) {
            fseek(GLOBAL_IO_ifp, read4bytes(), SEEK_SET);
        }
        jpeg_stdio_src(&cinfo, GLOBAL_IO_ifp);
        jpeg_read_header(&cinfo, TRUE);
        jpeg_start_decompress(&cinfo);
        buf = (*cinfo.mem->alloc_sarray)
                ((j_common_ptr) &cinfo, JPOOL_IMAGE, cinfo.output_width * 3, 1);
        while ( cinfo.output_scanline < cinfo.output_height &&
                (row = trow + cinfo.output_scanline) < height ) {
            jpeg_read_scanlines(&cinfo, buf, 1);
            pixel = (JSAMPLE (*)[3]) buf[0];
            for ( col = 0; col < cinfo.output_width && tcol + col < width; col++ ) {
                for ( c = 0; c < 3; c++ ) {
                    GLOBAL_image[row * width + tcol + col][c] = cur[c][pixel[col][c]];
                }
            }
        }
        jpeg_abort_decompress(&cinfo);
        if ((tcol += tile_width) >= THE_image.width ) {
            trow += tile_length + (tcol = 0);
        }
    }
    jpeg_destroy_decompress(&cinfo);
    ADOBE_maximum = 0xffff;
}

#endif

void
eight_bit_load_raw() {
    unsigned char *pixel;
    unsigned row;
    unsigned col;

    pixel = (unsigned char *) calloc(THE_image.width, sizeof *pixel);
    memoryError(pixel, "eight_bit_load_raw()");
    for ( row = 0; row < THE_image.height; row++ ) {
        if ( fread(pixel, 1, THE_image.width, GLOBAL_IO_ifp) < THE_image.width ) {
            inputOutputError();
        }
        for ( col = 0; col < THE_image.width; col++ ) {
            RAW(row, col) = GAMMA_curveFunctionLookupTable[pixel[col]];
        }
    }
    free(pixel);
    ADOBE_maximum = GAMMA_curveFunctionLookupTable[0xff];
}

#define HOLE(row) ((holes >> (((row) - THE_image.height) & 7)) & 1)

/* Kudos to Rich Taylor for figuring out SMaL's compression algorithm. */
void
smal_decode_segment(unsigned seg[2][2], int holes) {
    unsigned char hist[3][13] = {
            {7, 7, 0, 0, 63, 55, 47, 39, 31, 23, 15, 7, 0},
            {7, 7, 0, 0, 63, 55, 47, 39, 31, 23, 15, 7, 0},
            {3, 3, 0, 0, 63, 47, 31, 15, 0}};
    int low;
    int high = 0xff;
    int carry = 0;
    int nbits = 8;
    int pix;
    int s;
    int count;
    int bin;
    int next;
    int i;
    int sym[3];
    unsigned char diff;
    unsigned char pred[] = {0, 0};
    unsigned short data = 0;
    unsigned short range = 0;

    fseek(GLOBAL_IO_ifp, seg[0][1] + 1, SEEK_SET);
    getbits(-1);
    if ( seg[1][0] > THE_image.width * THE_image.height ) {
        seg[1][0] = THE_image.width * THE_image.height;
    }
    for ( pix = seg[0][0]; pix < seg[1][0]; pix++ ) {
        for ( s = 0; s < 3; s++ ) {
            data = data << nbits | getbits(nbits);
            if ( carry < 0 ) {
                carry = (nbits += carry + 1) < 1 ? nbits - 1 : 0;
            }
            while ( --nbits >= 0 ) {
                if ( (data >> nbits & 0xff) == 0xff ) {
                    break;
                }
            }
            if ( nbits > 0 ) {
                data = ((data & ((1 << (nbits - 1)) - 1)) << 1) |
                       ((data + (((data & (1 << (nbits - 1)))) << 1)) & (-1 << nbits));
            }
            if ( nbits >= 0 ) {
                data += getbits(1);
                carry = nbits - 8;
            }
            count = ((((data - range + 1) & 0xffff) << 2) - 1) / (high >> 4);
            for ( bin = 0; hist[s][bin + 5] > count; bin++ );
            low = hist[s][bin + 5] * (high >> 4) >> 2;
            if ( bin ) {
                high = hist[s][bin + 4] * (high >> 4) >> 2;
            }
            high -= low;
            for ( nbits = 0; high << nbits < 128; nbits++ );
            range = (range + low) << nbits;
            high <<= nbits;
            next = hist[s][1];
            if ( ++hist[s][2] > hist[s][3] ) {
                next = (next + 1) & hist[s][0];
                hist[s][3] = (hist[s][next + 4] - hist[s][next + 5]) >> 2;
                hist[s][2] = 1;
            }
            if ( hist[s][hist[s][1] + 4] - hist[s][hist[s][1] + 5] > 1 ) {
                if ( bin < hist[s][1] ) {
                    for ( i = bin; i < hist[s][1]; i++ ) {
                        hist[s][i + 5]--;
                    }
                } else {
                    if ( next <= bin ) {
                        for ( i = hist[s][1]; i < bin; i++ ) {
                            hist[s][i + 5]++;
                        }
                    }
                }
            }
            hist[s][1] = next;
            sym[s] = bin;
        }
        diff = sym[2] << 5 | sym[1] << 2 | (sym[0] & 3);
        if ( sym[0] & 4 ) {
            diff = diff ? -diff : 0x80;
        }
        if ( ftell(GLOBAL_IO_ifp) + 12 >= seg[1][1] ) {
            diff = 0;
        }
        THE_image.rawData[pix] = pred[pix & 1] += diff;
        if ( !(pix & 1) && HOLE(pix / THE_image.width) ) {
            pix += 2;
        }
    }
    ADOBE_maximum = 0xff;
}

void
smal_v6_load_raw() {
    unsigned seg[2][2];

    fseek(GLOBAL_IO_ifp, 16, SEEK_SET);
    seg[0][0] = 0;
    seg[0][1] = read2bytes();
    seg[1][0] = THE_image.width * THE_image.height;
    seg[1][1] = INT_MAX;
    smal_decode_segment(seg, 0);
}

int
median4(int *p) {
    int min;
    int max;
    int sum;
    int i;

    min = max = sum = p[0];
    for ( i = 1; i < 4; i++ ) {
        sum += p[i];
        if ( min > p[i] ) {
            min = p[i];
        }
        if ( max < p[i] ) {
            max = p[i];
        }
    }
    return (sum - min - max) >> 1;
}

void
fill_holes(int holes) {
    int row;
    int col;
    int val[4];

    for ( row = 2; row < height - 2; row++ ) {
        if ( !HOLE(row) ) {
            continue;
        }
        for ( col = 1; col < width - 1; col += 4 ) {
            val[0] = RAW(row - 1, col - 1);
            val[1] = RAW(row - 1, col + 1);
            val[2] = RAW(row + 1, col - 1);
            val[3] = RAW(row + 1, col + 1);
            RAW(row, col) = median4(val);
        }
        for ( col = 2; col < width - 2; col += 4 ) {
            if ( HOLE(row - 2) || HOLE(row + 2)) {
                RAW(row, col) = (RAW(row, col - 2) + RAW(row, col + 2)) >> 1;
            } else {
                val[0] = RAW(row, col - 2);
                val[1] = RAW(row, col + 2);
                val[2] = RAW(row - 2, col);
                val[3] = RAW(row + 2, col);
                RAW(row, col) = median4(val);
            }
        }
    }
}

void
smal_v9_load_raw() {
    unsigned seg[256][2];
    unsigned offset;
    unsigned nseg;
    unsigned holes;
    unsigned i;

    fseek(GLOBAL_IO_ifp, 67, SEEK_SET);
    offset = read4bytes();
    nseg = (unsigned char) fgetc(GLOBAL_IO_ifp);
    fseek(GLOBAL_IO_ifp, offset, SEEK_SET);
    for ( i = 0; i < nseg * 2; i++ ) {
        ((unsigned *) seg)[i] = read4bytes() + GLOBAL_IO_profileOffset * (i & 1);
    }
    fseek(GLOBAL_IO_ifp, 78, SEEK_SET);
    holes = fgetc(GLOBAL_IO_ifp);
    fseek(GLOBAL_IO_ifp, 88, SEEK_SET);
    seg[nseg][0] = THE_image.height * THE_image.width;
    seg[nseg][1] = read4bytes() + GLOBAL_IO_profileOffset;
    for ( i = 0; i < nseg; i++ ) {
        smal_decode_segment(seg + i, holes);
    }
    if ( holes ) {
        fill_holes(holes);
    }
}

void
redcine_load_raw() {
#ifndef NO_JASPER
  int c;
  int row;
  int col;
  jas_stream_t *in;
  jas_image_t *jimg;
  jas_matrix_t *jmat;
  jas_seqent_t *data;
  unsigned short *img;
  unsigned short *pix;

  jas_init();
  in = jas_stream_fopen (CAMERA_IMAGE_information.inputFilename, "rb");
  jas_stream_seek (in, GLOBAL_IO_profileOffset+20, SEEK_SET);
  jimg = jas_image_decode (in, -1, 0);
  if (!jimg) {
      longjmp (failure, 3);
  }
  jmat = jas_matrix_create(height/2, width/2);
  merror (jmat, "redcine_load_raw()");
  img = (unsigned short *)calloc ((height+2), (width+2)*2);
  merror (img, "redcine_load_raw()");
  for ( c = 0; c < 4; c++ ) {
    jas_image_readcmpt (jimg, c, 0, 0, width/2, height/2, jmat);
    data = jas_matrix_getref (jmat, 0, 0);
    for (row = c >> 1; row < height; row+=2) {
        for ( col = c & 1; col < width; col += 2 ) {
            img[(row + 1) * (width + 2) + col + 1] = data[(row / 2) * (width / 2) + col / 2];
        }
    }
  }
  for (col=1; col <= width; col++) {
    img[col] = img[2*(width+2)+col];
    img[(height+1)*(width+2)+col] = img[(height-1)*(width+2)+col];
  }
  for (row=0; row < height+2; row++) {
    img[row*(width+2)] = img[row*(width+2)+2];
    img[(row+1)*(width+2)-1] = img[(row+1)*(width+2)-3];
  }
  for (row=1; row <= height; row++) {
    pix = img + row*(width+2) + (col = 1 + (FC(row,1) & 1));
    for (   ; col <= width; col+=2, pix+=2) {
      c = (((pix[0] - 0x800) << 3) +
	pix[-(width+2)] + pix[width+2] + pix[-1] + pix[1]) >> 2;
      pix[0] = LIM(c,0,4095);
    }
  }
  for (row=0; row < height; row++) {
      for ( col = 0; col < width; col++ ) {
          RAW(row, col) = curve[img[(row + 1) * (width + 2) + col + 1]];
      }
  }
  free (img);
  jas_matrix_destroy (jmat);
  jas_image_destroy (jimg);
  jas_stream_close (in);
#endif
}

/* RESTRICTED code starts here */

void
foveon_decoder(unsigned size, unsigned code) {
    static unsigned huff[1024];
    struct decode *cur;
    int i;
    int len;

    if ( !code ) {
        for ( i = 0; i < size; i++ ) {
            huff[i] = read4bytes();
        }
        memset(first_decode, 0, sizeof first_decode);
        free_decode = first_decode;
    }
    cur = free_decode++;
    if ( free_decode > first_decode + 2048 ) {
        fprintf(stderr, _("%s: decoder table overflow\n"), CAMERA_IMAGE_information.inputFilename);
        longjmp(failure, 2);
    }
    if ( code ) {
        for ( i = 0; i < size; i++ ) {
            if ( huff[i] == code ) {
                cur->leaf = i;
                return;
            }
        }
    }
    if ( (len = code >> 27) > 26 ) {
        return;
    }
    code = (len + 1) << 27 | (code & 0x3ffffff) << 1;

    cur->branch[0] = free_decode;
    foveon_decoder(size, code);
    cur->branch[1] = free_decode;
    foveon_decoder(size, code + 1);
}

void
foveon_thumb() {
    unsigned bwide;
    unsigned row;
    unsigned col;
    unsigned bitbuf = 0;
    unsigned bit = 1;
    unsigned c;
    unsigned i;
    char *buf;
    struct decode *dindex;
    short pred[3];

    bwide = read4bytes();
    fprintf(ofp, "P6\n%d %d\n255\n", thumb_width, thumb_height);
    if ( bwide > 0 ) {
        if ( bwide < thumb_width * 3 ) {
            return;
        }
        buf = (char *) malloc(bwide);
        memoryError(buf, "foveon_thumb()");
        for ( row = 0; row < thumb_height; row++ ) {
            fread(buf, 1, bwide, GLOBAL_IO_ifp);
            fwrite(buf, 3, thumb_width, ofp);
        }
        free(buf);
        return;
    }
    foveon_decoder(256, 0);

    for ( row = 0; row < thumb_height; row++ ) {
        memset(pred, 0, sizeof pred);
        if ( !bit ) {
            read4bytes();
        }
        for ( bit = col = 0; col < thumb_width; col++ ) {
            for ( c = 0; c < 3; c++ ) {
                for ( dindex = first_decode; dindex->branch[0]; ) {
                    if ((bit = (bit - 1) & 31) == 31 ) {
                        for ( i = 0; i < 4; i++ ) {
                            bitbuf = (bitbuf << 8) + fgetc(GLOBAL_IO_ifp);
                        }
                    }
                    dindex = dindex->branch[bitbuf >> bit & 1];
                }
                pred[c] += dindex->leaf;
                fputc(pred[c], ofp);
            }
        }
    }
}

void
foveon_sd_load_raw() {
    struct decode *dindex;
    short diff[1024];
    unsigned bitbuf = 0;
    int pred[3];
    int row;
    int col;
    int bit = -1;
    int c;
    int i;

    readShorts((unsigned short *) diff, 1024);
    if ( !GLOBAL_loadFlags ) {
        foveon_decoder(1024, 0);
    }

    for ( row = 0; row < height; row++ ) {
        memset(pred, 0, sizeof pred);
        if ( !bit && !GLOBAL_loadFlags && atoi(GLOBAL_model + 2) < 14 ) {
            read4bytes();
        }
        for ( col = bit = 0; col < width; col++ ) {
            if ( GLOBAL_loadFlags ) {
                bitbuf = read4bytes();
                for ( c = 0; c < 3; c++ ) {
                    pred[2 - c] += diff[bitbuf >> c * 10 & 0x3ff];
                }
            } else {
                for ( c = 0; c < 3; c++ ) {
                    for ( dindex = first_decode; dindex->branch[0]; ) {
                        if ( (bit = (bit - 1) & 31) == 31 ) {
                            for ( i = 0; i < 4; i++ ) {
                                bitbuf = (bitbuf << 8) + fgetc(GLOBAL_IO_ifp);
                            }
                        }
                        dindex = dindex->branch[bitbuf >> bit & 1];
                    }
                    pred[c] += diff[dindex->leaf];
                    if ( pred[c] >> 16 && ~pred[c] >> 16 ) {
                        inputOutputError();
                    }
                }
            }
            for ( c = 0; c < 3; c++ ) {
                GLOBAL_image[row * width + col][c] = pred[c];
            }
        }
    }
}

void
foveon_huff(unsigned short *huff) {
    int i;
    int j;
    int clen;
    int code;

    huff[0] = 8;
    for ( i = 0; i < 13; i++ ) {
        clen = getc(GLOBAL_IO_ifp);
        code = getc(GLOBAL_IO_ifp);
        for ( j = 0; j < 256 >> clen; ) {
            huff[code + ++j] = clen << 8 | i;
        }
    }
    read2bytes();
}

void
foveon_dp_load_raw() {
    unsigned c;
    unsigned roff[4];
    unsigned row;
    unsigned col;
    unsigned diff;
    unsigned short huff[512], vpred[2][2], hpred[2];

    fseek(GLOBAL_IO_ifp, 8, SEEK_CUR);
    foveon_huff(huff);
    roff[0] = 48;
    for ( c = 0; c < 3; c++ ) {
        roff[c + 1] = -(-(roff[c] + read4bytes()) & -16);
    }
    for ( c = 0; c < 3; c++ ) {
        fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset + roff[c], SEEK_SET);
        getbits(-1);
        vpred[0][0] = vpred[0][1] = vpred[1][0] = vpred[1][1] = 512;
        for ( row = 0; row < height; row++ ) {
            for ( col = 0; col < width; col++ ) {
                diff = ljpeg_diff(huff);
                if ( col < 2 ) {
                    hpred[col] = vpred[row & 1][col] += diff;
                } else {
                    hpred[col & 1] += diff;
                }
                GLOBAL_image[row * width + col][c] = hpred[col & 1];
            }
        }
    }
}

void
foveon_load_camf() {
    unsigned type;
    unsigned wide;
    unsigned high;
    unsigned i;
    unsigned j;
    unsigned row;
    unsigned col;
    unsigned diff;
    unsigned short huff[258];
    unsigned short vpred[2][2] = {{512, 512},
                     {512, 512}};
    unsigned short hpred[2];

    fseek(GLOBAL_IO_ifp, GLOBAL_meta_offset, SEEK_SET);
    type = read4bytes();
    read4bytes();
    read4bytes();
    wide = read4bytes();
    high = read4bytes();
    if ( type == 2 ) {
        fread(meta_data, 1, meta_length, GLOBAL_IO_ifp);
        for ( i = 0; i < meta_length; i++ ) {
            high = (high * 1597 + 51749) % 244944;
            wide = high * (INT64) 301593171 >> 24;
            meta_data[i] ^= ((((high << 8) - wide) >> 1) + wide) >> 17;
        }
    } else {
        if ( type == 4 ) {
            free(meta_data);
            meta_data = (char *) malloc(meta_length = wide * high * 3 / 2);
            memoryError(meta_data, "foveon_load_camf()");
            foveon_huff(huff);
            read4bytes();
            getbits(-1);
            for ( j = row = 0; row < high; row++ ) {
                for ( col = 0; col < wide; col++ ) {
                    diff = ljpeg_diff(huff);
                    if ( col < 2 ) hpred[col] = vpred[row & 1][col] += diff;
                    else hpred[col & 1] += diff;
                    if ( col & 1 ) {
                        meta_data[j++] = hpred[0] >> 4;
                        meta_data[j++] = hpred[0] << 4 | hpred[1] >> 8;
                        meta_data[j++] = hpred[1];
                    }
                }
            }
        } else {
            fprintf(stderr, _("%s has unknown CAMF type %d.\n"), CAMERA_IMAGE_information.inputFilename, type);
        }
    }
}

const char *
foveon_camf_param(const char *block, const char *param) {
    unsigned idx;
    unsigned num;
    char *pos;
    char *cp;
    char *dp;

    for ( idx = 0; idx < meta_length; idx += unsignedEndianSwap(pos + 8) ) {
        pos = meta_data + idx;
        if ( strncmp(pos, "CMb", 3)) {
            break;
        }
        if ( pos[3] != 'P' ) {
            continue;
        }
        if ( strcmp(block, pos + unsignedEndianSwap(pos + 12)) ) {
            continue;
        }
        cp = pos + unsignedEndianSwap(pos + 16);
        num = unsignedEndianSwap(cp);
        dp = pos + unsignedEndianSwap(cp + 4);
        while ( num-- ) {
            cp += 8;
            if ( !strcmp(param, dp + unsignedEndianSwap(cp)) ) {
                return dp + unsignedEndianSwap(cp + 4);
            }
        }
    }
    return 0;
}

void *
foveon_camf_matrix(unsigned dim[3], const char *name) {
    unsigned i;
    unsigned idx;
    unsigned type;
    unsigned ndim;
    unsigned size;
    unsigned *mat;
    char *pos;
    char *cp;
    char *dp;
    double dsize;

    for ( idx = 0; idx < meta_length; idx += unsignedEndianSwap(pos + 8) ) {
        pos = meta_data + idx;
        if ( strncmp(pos, "CMb", 3)) {
            break;
        }
        if ( pos[3] != 'M' ) {
            continue;
        }
        if ( strcmp(name, pos + unsignedEndianSwap(pos + 12)) ) {
            continue;
        }
        dim[0] = dim[1] = dim[2] = 1;
        cp = pos + unsignedEndianSwap(pos + 16);
        type = unsignedEndianSwap(cp);
        if ( (ndim = unsignedEndianSwap(cp + 4)) > 3 ) {
            break;
        }
        dp = pos + unsignedEndianSwap(cp + 8);
        for ( i = ndim; i--; ) {
            cp += 12;
            dim[i] = unsignedEndianSwap(cp);
        }
        if ( (dsize = (double) dim[0] * dim[1] * dim[2]) > meta_length / 4 ) {
            break;
        }
        mat = (unsigned *)malloc((size = dsize) * 4);
        memoryError(mat, "foveon_camf_matrix()");
        for ( i = 0; i < size; i++ ) {
            if ( type && type != 6 ) {
                mat[i] = unsignedEndianSwap(dp + i * 4);
            } else {
                mat[i] = unsignedEndianSwap(dp + i * 2) & 0xffff;
            }
        }
        return mat;
    }
    fprintf(stderr, _("%s: \"%s\" matrix not found!\n"), CAMERA_IMAGE_information.inputFilename, name);
    return 0;
}

int
foveon_fixed(void *ptr, int size, const char *name) {
    void *dp;
    unsigned dim[3];

    if ( !name ) {
        return 0;
    }
    dp = foveon_camf_matrix(dim, name);
    if ( !dp ) {
        return 0;
    }
    memcpy(ptr, dp, size * 4);
    free(dp);
    return 1;
}

float
foveon_avg(short *pix, int range[2], float cfilt) {
    int i;
    float val;
    float min = FLT_MAX;
    float max = -FLT_MAX;
    float sum = 0;

    for ( i = range[0]; i <= range[1]; i++ ) {
        sum += val = pix[i * 4] + (pix[i * 4] - pix[(i - 1) * 4]) * cfilt;
        if ( min > val ) {
            min = val;
        }
        if ( max < val ) {
            max = val;
        }
    }
    if ( range[1] - range[0] == 1 ) {
        return sum / 2;
    }
    return (sum - min - max) / (range[1] - range[0] - 1);
}

short *
foveon_make_curve(double max, double mul, double filt) {
    short *curve;
    unsigned i;
    unsigned size;
    double x;

    if ( !filt ) {
        filt = 0.8;
    }
    size = 4 * M_PI * max / filt;
    if ( size == UINT_MAX) {
        size--;
    }
    curve = (short *)calloc(size + 1, sizeof *curve);
    memoryError(curve, "foveon_make_curve()");
    curve[0] = size;
    for ( i = 0; i < size; i++ ) {
        x = i * filt / max / 4;
        curve[i + 1] = (cos(x) + 1) / 2 * tanh(i * filt / mul) * mul + 0.5;
    }
    return curve;
}

void
foveon_make_curves(short **curvep, float dq[3], float div[3], float filt) {
    double mul[3];
    double max = 0;
    int c;

    for ( c = 0; c < 3; c++ ) {
        mul[c] = dq[c] / div[c];
    }
    for ( c = 0; c < 3; c++ ) {
        if ( max < mul[c] ) max = mul[c];
    }
    for ( c = 0; c < 3; c++ ) {
        curvep[c] = foveon_make_curve(max, mul[c], filt);
    }
}

#define image ((short (*)[4]) GLOBAL_image)

int
foveon_apply_curve(short *foveonCurve, int i) {
    if ( abs(i) >= foveonCurve[0] ) {
        return 0;
    }
    return i < 0 ? -foveonCurve[1 - i] : foveonCurve[1 + i];
}

void
foveon_interpolate() {
    static const short hood[] = {-1, -1, -1, 0, -1, 1, 0, -1, 0, 1, 1, -1, 1, 0, 1, 1};
    short *pix;
    short prev[3];
    short *foveonCurve[8];
    short (*foveonShrink)[3];
    float cfilt = 0;
    float ddft[3][3][2];
    float ppm[3][3][3];
    float cam_xyz[3][3];
    float correct[3][3];
    float last[3][3];
    float trans[3][3];
    float chroma_dq[3];
    float color_dq[3];
    float diag[3][3];
    float div[3];
    float (*foveonBlack)[3];
    float (*sgain)[3];
    float (*sgrow)[3];
    float fsum[3];
    float val;
    float frow;
    float num;
    int row;
    int col;
    int c;
    int i;
    int j;
    int diff;
    int sgx;
    int irow;
    int sum;
    int min;
    int max;
    int limit;
    int dscr[2][2];
    int dstb[4];
    int (*smrow[7])[3];
    int total[4];
    int ipix[3];
    int work[3][3];
    int smlast;
    int smred;
    int smred_p = 0;
    int dev[3];
    int satlev[3];
    int keep[4];
    int active[4];
    unsigned dim[3];
    unsigned *badpix;
    double dsum = 0;
    double trsum[3];
    char str[128];
    const char *cp;

    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, _("Foveon interpolation...\n"));
    }

    foveon_load_camf();
    foveon_fixed(dscr, 4, "DarkShieldColRange");
    foveon_fixed(ppm[0][0], 27, "PostPolyMatrix");
    foveon_fixed(satlev, 3, "SaturationLevel");
    foveon_fixed(keep, 4, "KeepImageArea");
    foveon_fixed(active, 4, "ActiveImageArea");
    foveon_fixed(chroma_dq, 3, "ChromaDQ");
    foveon_fixed(color_dq, 3,
                 foveon_camf_param("IncludeBlocks", "ColorDQ") ?
                 "ColorDQ" : "ColorDQCamRGB");
    if ( foveon_camf_param("IncludeBlocks", "ColumnFilter") ) {
        foveon_fixed(&cfilt, 1, "ColumnFilter");
    }

    memset(ddft, 0, sizeof ddft);
    if ( !foveon_camf_param("IncludeBlocks", "DarkDrift")
         || !foveon_fixed(ddft[1][0], 12, "DarkDrift") ) {
        for ( i = 0; i < 2; i++ ) {
            foveon_fixed(dstb, 4, i ? "DarkShieldBottom" : "DarkShieldTop");
            for ( row = dstb[1]; row <= dstb[3]; row++ ) {
                for ( col = dstb[0]; col <= dstb[2]; col++ ) {
                    for ( c = 0; c < 3; c++ ) {
                        ddft[i + 1][c][1] += (short)GLOBAL_image[row * width + col][c];
                    }
                }
            }
            for ( c = 0; c < 3; c++ ) {
                ddft[i + 1][c][1] /= (dstb[3] - dstb[1] + 1) * (dstb[2] - dstb[0] + 1);
            }
        }
    }

    if ( !(cp = foveon_camf_param("WhiteBalanceIlluminants", model2)) ) {
        fprintf(stderr, _("%s: Invalid white balance \"%s\"\n"), CAMERA_IMAGE_information.inputFilename, model2);
        return;
    }
    foveon_fixed(cam_xyz, 9, cp);
    foveon_fixed(correct, 9,
                 foveon_camf_param("WhiteBalanceCorrections", model2));
    memset(last, 0, sizeof last);
    for ( i = 0; i < 3; i++ ) {
        for ( j = 0; j < 3; j++ ) {
            for ( c = 0; c < 3; c++ ) {
                last[i][j] += correct[i][c] * cam_xyz[c][j];
            }
        }
    }

    #define LAST(x, y) last[(i+x)%3][(c+y)%3]
    for ( i = 0; i < 3; i++ ) {
        for ( c = 0; c < 3; c++ ) {
            diag[c][i] = LAST(1, 1) * LAST(2, 2) - LAST(1, 2) * LAST(2, 1);
        }
    }
    #undef LAST

    for ( c = 0; c < 3; c++ ) {
        div[c] = diag[c][0] * 0.3127 + diag[c][1] * 0.329 + diag[c][2] * 0.3583;
    }
    snprintf(str, 128, "%sRGBNeutral", model2);
    if ( foveon_camf_param("IncludeBlocks", str)) {
        foveon_fixed(div, 3, str);
    }
    num = 0;
    for ( c = 0; c < 3; c++ ) {
        if ( num < div[c] ) num = div[c];
    }
    for ( c = 0; c < 3; c++ ) {
        div[c] /= num;
    }

    memset(trans, 0, sizeof trans);
    for ( i = 0; i < 3; i++ ) {
        for ( j = 0; j < 3; j++ ) {
            for ( c = 0; c < 3; c++ ) {
                trans[i][j] += rgb_cam[i][c] * last[c][j] * div[j];
            }
        }
    }
    for ( c = 0; c < 3; c++ ) {
        trsum[c] = trans[c][0] + trans[c][1] + trans[c][2];
    }
    dsum = (6 * trsum[0] + 11 * trsum[1] + 3 * trsum[2]) / 20;
    for ( i = 0; i < 3; i++ ) {
        for ( c = 0; c < 3; c++ ) {
            last[i][c] = trans[i][c] * dsum / trsum[i];
        }
    }
    memset(trans, 0, sizeof trans);
    for ( i = 0; i < 3; i++ ) {
        for ( j = 0; j < 3; j++ ) {
            for ( c = 0; c < 3; c++ ) {
                trans[i][j] += (i == c ? 32 : -1) * last[c][j] / 30;
            }
        }
    }

    foveon_make_curves(foveonCurve, color_dq, div, cfilt);
    for ( c = 0; c < 3; c++ ) {
        chroma_dq[c] /= 3;
    }
    foveon_make_curves(foveonCurve + 3, chroma_dq, div, cfilt);
    for ( c = 0; c < 3; c++ ) {
        dsum += chroma_dq[c] / div[c];
    }
    foveonCurve[6] = foveon_make_curve(dsum, dsum, cfilt);
    foveonCurve[7] = foveon_make_curve(dsum * 2, dsum * 2, cfilt);

    sgain = (float (*)[3]) foveon_camf_matrix(dim, "SpatialGain");
    if ( !sgain ) {
        return;
    }
    sgrow = (float (*)[3]) calloc(dim[1], sizeof *sgrow);
    sgx = (width + dim[1] - 2) / (dim[1] - 1);

    foveonBlack = (float (*)[3]) calloc(height, sizeof *foveonBlack);
    for ( row = 0; row < height; row++ ) {
        for ( i = 0; i < 6; i++ ) {
            ((float *) ddft[0])[i] = ((float *) ddft[1])[i] +
                                     row / (height - 1.0) * (((float *) ddft[2])[i] - ((float *) ddft[1])[i]);
        }
        for ( c = 0; c < 3; c++ ) {
            foveonBlack[row][c] =
                    (foveon_avg(image[row * width] + c, dscr[0], cfilt) +
                     foveon_avg(image[row * width] + c, dscr[1], cfilt) * 3
                     - ddft[0][c][0]) / 4 - ddft[0][c][1];
        }
    }
    memcpy(foveonBlack, foveonBlack + 8, sizeof *foveonBlack * 8);
    memcpy(foveonBlack + height - 11, foveonBlack + height - 22, 11 * sizeof *foveonBlack);
    memcpy(last, foveonBlack, sizeof last);

    for ( row = 1; row < height - 1; row++ ) {
        for ( c = 0; c < 3; c++ ) {
            if ( last[1][c] > last[0][c] ) {
                if ( last[1][c] > last[2][c] ) {
                    foveonBlack[row][c] = (last[0][c] > last[2][c]) ? last[0][c] : last[2][c];
                }
            } else {
                if ( last[1][c] < last[2][c] ) {
                    foveonBlack[row][c] = (last[0][c] < last[2][c]) ? last[0][c] : last[2][c];
                }
            }
        }
        memmove(last, last + 1, 2 * sizeof last[0]);
        memcpy(last[2], foveonBlack[row + 1], sizeof last[2]);
    }
    for ( c = 0; c < 3; c++ ) {
        foveonBlack[row][c] = (last[0][c] + last[1][c]) / 2;
    }
    for ( c = 0; c < 3; c++ ) {
        foveonBlack[0][c] = (foveonBlack[1][c] + foveonBlack[3][c]) / 2;
    }

    val = 1 - exp(-1 / 24.0);
    memcpy(fsum, foveonBlack, sizeof fsum);
    for ( row = 1; row < height; row++ ) {
        for ( c = 0; c < 3; c++ ) {
            fsum[c] += foveonBlack[row][c] =
                    (foveonBlack[row][c] - foveonBlack[row - 1][c]) * val + foveonBlack[row - 1][c];
        }
    }
    memcpy(last[0], foveonBlack[height - 1], sizeof last[0]);
    for ( c = 0; c < 3; c++ ) {
        fsum[c] /= height;
    }
    for ( row = height; row--; ) {
        for ( c = 0; c < 3; c++ ) {
            last[0][c] = foveonBlack[row][c] =
                    (foveonBlack[row][c] - fsum[c] - last[0][c]) * val + last[0][c];
        }
    }

    memset(total, 0, sizeof total);
    for ( row = 2; row < height; row += 4 ) {
        for ( col = 2; col < width; col += 4 ) {
            for ( c = 0; c < 3; c++ ) {
                total[c] += (short) image[row * width + col][c];
            }
            total[3]++;
        }
    }
    for ( row = 0; row < height; row++ ) {
        for ( c = 0; c < 3; c++ ) {
            foveonBlack[row][c] += fsum[c] / 2 + total[c] / (total[3] * 100.0);
        }
    }

    for ( row = 0; row < height; row++ ) {
        for ( i = 0; i < 6; i++ ) {
            ((float *) ddft[0])[i] = ((float *) ddft[1])[i] +
                                     row / (height - 1.0) * (((float *) ddft[2])[i] - ((float *) ddft[1])[i]);
        }
        pix = image[row * width];
        memcpy(prev, pix, sizeof prev);
        frow = row / (height - 1.0) * (dim[2] - 1);
        if ( (irow = frow) == dim[2] - 1 ) {
            irow--;
        }
        frow -= irow;
        for ( i = 0; i < dim[1]; i++ ) {
            for ( c = 0; c < 3; c++ ) {
                sgrow[i][c] = sgain[irow * dim[1] + i][c] * (1 - frow) +
                              sgain[(irow + 1) * dim[1] + i][c] * frow;
            }
        }
        for ( col = 0; col < width; col++ ) {
            for ( c = 0; c < 3; c++ ) {
                diff = pix[c] - prev[c];
                prev[c] = pix[c];
                ipix[c] = pix[c] + floor((diff + (diff * diff >> 14)) * cfilt
                                         - ddft[0][c][1] - ddft[0][c][0] * ((float) col / width - 0.5)
                                         - foveonBlack[row][c]);
            }
            for ( c = 0; c < 3; c++ ) {
                work[0][c] = ipix[c] * ipix[c] >> 14;
                work[2][c] = ipix[c] * work[0][c] >> 14;
                work[1][2 - c] = ipix[(c + 1) % 3] * ipix[(c + 2) % 3] >> 14;
            }
            for ( c = 0; c < 3; c++ ) {
                for ( val = i = 0; i < 3; i++ ) {
                    for ( j = 0; j < 3; j++ ) {
                        val += ppm[c][i][j] * work[i][j];
                    }
                }
                ipix[c] = floor((ipix[c] + floor(val)) *
                                (sgrow[col / sgx][c] * (sgx - col % sgx) +
                                 sgrow[col / sgx + 1][c] * (col % sgx)) / sgx / div[c]);
                if ( ipix[c] > 32000 ) {
                    ipix[c] = 32000;
                }
                pix[c] = ipix[c];
            }
            pix += 4;
        }
    }
    free(foveonBlack);
    free(sgrow);
    free(sgain);

    if ( (badpix = (unsigned *) foveon_camf_matrix(dim, "BadPixels")) ) {
        for ( i = 0; i < dim[0]; i++ ) {
            col = (badpix[i] >> 8 & 0xfff) - keep[0];
            row = (badpix[i] >> 20) - keep[1];
            if ((unsigned) (row - 1) > height - 3 || (unsigned) (col - 1) > width - 3 )
                continue;
            memset(fsum, 0, sizeof fsum);
            for ( sum = j = 0; j < 8; j++ ) {
                if ( badpix[i] & (1 << j)) {
                    for ( c = 0; c < 3; c++ ) {
                        fsum[c] += (short)
                                image[(row + hood[j * 2]) * width + col + hood[j * 2 + 1]][c];
                    }
                    sum++;
                }
            }
            if ( sum ) {
                for ( c = 0; c < 3; c++ ) {
                    image[row * width + col][c] = fsum[c] / sum;
                }
            }
        }
        free(badpix);
    }

    // Array for 5x5 Gaussian averaging of red values
    smrow[6] = (int (*)[3]) calloc(width * 5, sizeof **smrow);
    memoryError(smrow[6], "foveon_interpolate()");
    for ( i = 0; i < 5; i++ ) {
        smrow[i] = smrow[6] + i * width;
    }

    // Sharpen the reds against these Gaussian averages
    for ( smlast = -1, row = 2; row < height - 2; row++ ) {
        while ( smlast < row + 2 ) {
            for ( i = 0; i < 6; i++ ) {
                smrow[(i + 5) % 6] = smrow[i];
            }
            pix = image[++smlast * width + 2];
            for ( col = 2; col < width - 2; col++ ) {
                smrow[4][col][0] =
                        (pix[0] * 6 + (pix[-4] + pix[4]) * 4 + pix[-8] + pix[8] + 8) >> 4;
                pix += 4;
            }
        }
        pix = image[row * width + 2];
        for ( col = 2; col < width - 2; col++ ) {
            smred = (6 * smrow[2][col][0]
                     + 4 * (smrow[1][col][0] + smrow[3][col][0])
                     + smrow[0][col][0] + smrow[4][col][0] + 8) >> 4;
            if ( col == 2 ) {
                smred_p = smred;
            }
            i = pix[0] + ((pix[0] - ((smred * 7 + smred_p) >> 3)) >> 3);
            if ( i > 32000 ) {
                i = 32000;
            }
            pix[0] = i;
            smred_p = smred;
            pix += 4;
        }
    }

    // Adjust the brighter pixels for better linearity
    min = 0xffff;
    for ( c = 0; c < 3; c++ ) {
        i = satlev[c] / div[c];
        if ( min > i ) {
            min = i;
        }
    }
    limit = min * 9 >> 4;
    for ( pix = image[0]; pix < image[height * width]; pix += 4 ) {
        if ( pix[0] <= limit || pix[1] <= limit || pix[2] <= limit ) {
            continue;
        }
        min = max = pix[0];
        for ( c = 1; c < 3; c++ ) {
            if ( min > pix[c] ) {
                min = pix[c];
            }
            if ( max < pix[c] ) {
                max = pix[c];
            }
        }
        if ( min >= limit * 2 ) {
            pix[0] = pix[1] = pix[2] = max;
        } else {
            i = 0x4000 - ((min - limit) << 14) / limit;
            i = 0x4000 - (i * i >> 14);
            i = i * i >> 14;
            for ( c = 0; c < 3; c++ ) {
                pix[c] += (max - pix[c]) * i >> 14;
            }
        }
    }

    /*
    Because photons that miss one detector often hit another,
    the sum R+G+B is much less noisy than the individual colors.
    So smooth the hues without smoothing the total.
    */
    for ( smlast = -1, row = 2; row < height - 2; row++ ) {
        while ( smlast < row + 2 ) {
            for ( i = 0; i < 6; i++ ) {
                smrow[(i + 5) % 6] = smrow[i];
            }
            pix = image[++smlast * width + 2];
            for ( col = 2; col < width - 2; col++ ) {
                for ( c = 0; c < 3; c++ ) {
                    smrow[4][col][c] = (pix[c - 4] + 2 * pix[c] + pix[c + 4] + 2) >> 2;
                }
                pix += 4;
            }
        }
        pix = image[row * width + 2];
        for ( col = 2; col < width - 2; col++ ) {
            for ( c = 0; c < 3; c++ ) {
                dev[c] = -foveon_apply_curve(foveonCurve[7], pix[c] -
                                                             ((smrow[1][col][c] + 2 * smrow[2][col][c] + smrow[3][col][c])
                                                               >> 2));
            }
            sum = (dev[0] + dev[1] + dev[2]) >> 3;
            for ( c = 0; c < 3; c++ ) {
                pix[c] += dev[c] - sum;
            };
            pix += 4;
        }
    }
    for ( smlast = -1, row = 2; row < height - 2; row++ ) {
        while ( smlast < row + 2 ) {
            for ( i = 0; i < 6; i++ ) {
                smrow[(i + 5) % 6] = smrow[i];
            }
            pix = image[++smlast * width + 2];
            for ( col = 2; col < width - 2; col++ ) {
                for ( c = 0; c < 3; c++ ) {
                    smrow[4][col][c] =
                            (pix[c - 8] + pix[c - 4] + pix[c] + pix[c + 4] + pix[c + 8] + 2) >> 2;
                }
                pix += 4;
            }
        }
        pix = image[row * width + 2];
        for ( col = 2; col < width - 2; col++ ) {
            for ( total[3] = 375, sum = 60, c = 0; c < 3; c++ ) {
                for ( total[c] = i = 0; i < 5; i++ ) {
                    total[c] += smrow[i][col][c];
                }
                total[3] += total[c];
                sum += pix[c];
            }
            if ( sum < 0 ) {
                sum = 0;
            }
            j = total[3] > 375 ? (sum << 16) / total[3] : sum * 174;
            for ( c = 0; c < 3; c++ ) {
                pix[c] += foveon_apply_curve(foveonCurve[6],
                                             ((j * total[c] + 0x8000) >> 16) - pix[c]);
            }
            pix += 4;
        }
    }

    // Transform the GLOBAL_image to a different colorspace
    for ( pix = image[0]; pix < image[height * width]; pix += 4 ) {
        for ( c = 0; c < 3; c++ ) {
            pix[c] -= foveon_apply_curve(foveonCurve[c], pix[c]);
        }
        sum = (pix[0] + pix[1] + pix[1] + pix[2]) >> 2;
        for ( c = 0; c < 3; c++ ) {
            pix[c] -= foveon_apply_curve(foveonCurve[c], pix[c] - sum);
        }
        for ( c = 0; c < 3; c++ ) {
            for ( dsum = i = 0; i < 3; i++ )
                dsum += trans[c][i] * pix[i];
            if ( dsum < 0 ) {
                dsum = 0;
            }
            if ( dsum > 24000 ) {
                dsum = 24000;
            }
            ipix[c] = dsum + 0.5;
        }
        for ( c = 0; c < 3; c++ ) {
            pix[c] = ipix[c];
        }
    }

    // Smooth the GLOBAL_image bottom-to-top and save at 1/4 scale
    foveonShrink = (short (*)[3]) calloc((height / 4), (width / 4) * sizeof *foveonShrink);
    memoryError(foveonShrink, "foveon_interpolate()");
    for ( row = height / 4; row--; ) {
        for ( col = 0; col < width / 4; col++ ) {
            ipix[0] = ipix[1] = ipix[2] = 0;
            for ( i = 0; i < 4; i++ ) {
                for ( j = 0; j < 4; j++ ) {
                    for ( c = 0; c < 3; c++ ) {
                        ipix[c] += image[(row * 4 + i) * width + col * 4 + j][c];
                    }
                }
            }
            for ( c = 0; c < 3; c++ ) {
                if ( row + 2 > height / 4 ) {
                    foveonShrink[row * (width / 4) + col][c] = ipix[c] >> 4;
                } else {
                    foveonShrink[row * (width / 4) + col][c] =
                            (foveonShrink[(row + 1) * (width / 4) + col][c] * 1840 + ipix[c] * 141 + 2048) >> 12;
                }
            }
        }
    }

    // From the 1/4-scale GLOBAL_image, smooth right-to-left
    for ( row = 0; row < (height & ~3); row++ ) {
        ipix[0] = ipix[1] = ipix[2] = 0;
        if ( (row & 3) == 0 ) {
            for ( col = width & ~3; col--; ) {
                for ( c = 0; c < 3; c++ ) {
                    smrow[0][col][c] = ipix[c] =
                            (foveonShrink[(row / 4) * (width / 4) + col / 4][c] * 1485 + ipix[c] * 6707 + 4096) >> 13;
                }
            }
        }

        // Then smooth left-to-right
        ipix[0] = ipix[1] = ipix[2] = 0;
        for ( col = 0; col < (width & ~3); col++ ) {
            for ( c = 0; c < 3; c++ ) {
                smrow[1][col][c] = ipix[c] =
                        (smrow[0][col][c] * 1485 + ipix[c] * 6707 + 4096) >> 13;
            }
        }

        // Smooth top-to-bottom
        if ( row == 0 ) {
            memcpy(smrow[2], smrow[1], sizeof **smrow * width);
        } else {
            for ( col = 0; col < (width & ~3); col++ ) {
                for ( c = 0; c < 3; c++ ) {
                    smrow[2][col][c] =
                            (smrow[2][col][c] * 6707 + smrow[1][col][c] * 1485 + 4096) >> 13;
                }
            }
        }

        // Adjust the chroma toward the smooth values
        for ( col = 0; col < (width & ~3); col++ ) {
            for ( i = j = 30, c = 0; c < 3; c++ ) {
                i += smrow[2][col][c];
                j += image[row * width + col][c];
            }
            j = (j << 16) / i;
            for ( sum = c = 0; c < 3; c++ ) {
                ipix[c] = foveon_apply_curve(foveonCurve[c + 3],
                                             ((smrow[2][col][c] * j + 0x8000) >> 16) - image[row * width + col][c]);
                sum += ipix[c];
            }
            sum >>= 3;
            for ( c = 0; c < 3; c++ ) {
                i = image[row * width + col][c] + ipix[c] - sum;
                if ( i < 0 ) {
                    i = 0;
                }
                image[row * width + col][c] = i;
            }
        }
    }
    free(foveonShrink);
    free(smrow[6]);
    for ( i = 0; i < 8; i++ ) {
        free(foveonCurve[i]);
    }

    // Trim off the foveonBlack border
    active[1] -= keep[1];
    active[3] -= 2;
    i = active[2] - active[0];
    for ( row = 0; row < active[3] - active[1]; row++ ) {
        memcpy(image[row * i], image[(row + active[1]) * width + active[0]],
               i * sizeof *image);
    }
    width = i;
    height = row;
}

#undef image

/* RESTRICTED code ends here */

void
crop_masked_pixels() {
    int row;
    int col;
    unsigned r;
    unsigned c;
    unsigned m;
    unsigned mblack[8];
    unsigned zero;
    unsigned val;

    if ( TIFF_CALLBACK_loadRawData == & phase_one_load_raw ||
         TIFF_CALLBACK_loadRawData == & phase_one_load_raw_c ) {
        phase_one_correct();
    }
    if ( fuji_width ) {
        for ( row = 0; row < THE_image.height - top_margin * 2; row++ ) {
            for ( col = 0; col < fuji_width << !fuji_layout; col++ ) {
                if ( fuji_layout ) {
                    r = fuji_width - 1 - col + (row >> 1);
                    c = col + ((row + 1) >> 1);
                } else {
                    r = fuji_width - 1 + row - (col >> 1);
                    c = row + ((col + 1) >> 1);
                }
                if ( r < height && c < width ) {
                    BAYER(r, c) = RAW(row + top_margin, col + left_margin);
                }
            }
        }
    } else {
        for ( row = 0; row < height; row++ ) {
            for ( col = 0; col < width; col++ ) {
                BAYER2(row, col) = RAW(row + top_margin, col + left_margin);
            }
        }
    }
    if ( mask[0][3] > 0 ) {
        goto mask_set;
    }
    if ( TIFF_CALLBACK_loadRawData == &canon_load_raw ||
         TIFF_CALLBACK_loadRawData == &lossless_jpeg_load_raw ) {
        mask[0][1] = mask[1][1] += 2;
        mask[0][3] -= 2;
        goto sides;
    }
    if ( TIFF_CALLBACK_loadRawData == &canon_600_load_raw ||
         TIFF_CALLBACK_loadRawData == &sony_load_raw ||
         (TIFF_CALLBACK_loadRawData == &eight_bit_load_raw && strncmp(GLOBAL_model, "DC2", 3)) ||
         TIFF_CALLBACK_loadRawData == &kodak_262_load_raw ||
         (TIFF_CALLBACK_loadRawData == &packed_load_raw && (GLOBAL_loadFlags & 256))) {
        sides:
        mask[0][0] = mask[1][0] = top_margin;
        mask[0][2] = mask[1][2] = top_margin + height;
        mask[0][3] += left_margin;
        mask[1][1] += left_margin + width;
        mask[1][3] += THE_image.width;
    }
    if ( TIFF_CALLBACK_loadRawData == &nokia_load_raw ) {
        mask[0][2] = top_margin;
        mask[0][3] = width;
    }
    mask_set:
    memset(mblack, 0, sizeof mblack);
    for ( zero = m = 0; m < 8; m++ ) {
        for ( row = MAX(mask[m][0], 0); row < MIN(mask[m][2], THE_image.height); row++ ) {
            for ( col = MAX(mask[m][1], 0); col < MIN(mask[m][3], THE_image.width); col++ ) {
                c = FC(row - top_margin, col - left_margin);
                mblack[c] += val = RAW(row, col);
                mblack[4 + c]++;
                zero += !val;
            }
        }
    }
    if ( TIFF_CALLBACK_loadRawData == &canon_600_load_raw && width < THE_image.width ) {
        ADOBE_black = (mblack[0] + mblack[1] + mblack[2] + mblack[3]) /
                      (mblack[4] + mblack[5] + mblack[6] + mblack[7]) - 4;
        canon_600_correct();
    } else {
        if ( zero < mblack[4] && mblack[5] && mblack[6] && mblack[7] ) {
            for ( c = 0; c < 4; c++ ) {
                cblack[c] = mblack[c] / mblack[4 + c];
            }
            cblack[4] = cblack[5] = cblack[6] = 0;
        }
    }
}

void
remove_zeroes() {
    unsigned row;
    unsigned col;
    unsigned tot;
    unsigned n;
    unsigned r;
    unsigned c;

    for ( row = 0; row < height; row++ ) {
        for ( col = 0; col < width; col++ ) {
            if ( BAYER(row, col) == 0 ) {
                tot = n = 0;
                for ( r = row - 2; r <= row + 2; r++ ) {
                    for ( c = col - 2; c <= col + 2; c++ ) {
                        if ( r < height && c < width &&
                             FC(r, c) == FC(row, col) && BAYER(r, c) ) {
                            tot += (n++, BAYER(r, c));
                        }
                    }
                }
                if ( n ) {
                    BAYER(row, col) = tot / n;
                }
            }
        }
    }
}

/*
Seach from the current directory up to the root looking for
a ".badpixels" file, and fix those pixels now.
 */
void
bad_pixels(const char *cfname) {
    FILE *fp = 0;
    char *fname;
    char *cp;
    char line[128];
    int len;
    int time;
    int row;
    int col;
    int r;
    int c;
    int rad;
    int tot;
    int n;
    int fixed = 0;

    if ( !IMAGE_filters ) {
        return;
    }
    if ( cfname ) {
        fp = fopen(cfname, "r");
    } else {
        for ( len = 32;; len *= 2 ) {
            fname = (char *)malloc(len);
            if ( !fname ) {
                return;
            }
            if ( getcwd(fname, len - 16) ) {
                break;
            }
            free(fname);
            if ( errno != ERANGE ) {
                return;
            }
        }
#if defined(WIN32) || defined(DJGPP)
    memmove (fname, fname+2, len-2);
    for ( cp = fname; *cp; cp++ ) {
      if ( *cp == '\\' ) {
          *cp = '/';
      }
    }
#endif
        cp = fname + strlen(fname);
        if ( cp[-1] == '/' ) {
            cp--;
        }
        while ( *fname == '/' ) {
            strcpy(cp, "/.badpixels");
            if ( (fp = fopen(fname, "r")) ) {
                break;
            }
            if ( cp == fname ) {
                break;
            }
            while ( *--cp != '/' );
        }
        free(fname);
    }
    if ( !fp ) {
        return;
    }
    while ( fgets(line, 128, fp) ) {
        cp = strchr(line, '#');
        if ( cp ) {
            *cp = 0;
        }
        if ( sscanf(line, "%d %d %d", &col, &row, &time) != 3 ) {
            continue;
        }
        if ( (unsigned) col >= width || (unsigned) row >= height ) {
            continue;
        }
        if ( time > timestamp ) {
            continue;
        }
        for ( tot = n = 0, rad = 1; rad < 3 && n == 0; rad++ ) {
            for ( r = row - rad; r <= row + rad; r++ ) {
                for ( c = col - rad; c <= col + rad; c++ ) {
                    if ((unsigned) r < height && (unsigned) c < width &&
                        (r != row || c != col) && fcol(r, c) == fcol(row, col)) {
                        tot += BAYER2(r, c);
                        n++;
                    }
                }
            }
        }
        BAYER2(row, col) = tot / n;
        if ( OPTIONS_values->verbose ) {
            if ( !fixed++ ) {
                fprintf(stderr, _("Fixed dead pixels at:"));
            }
            fprintf(stderr, " %d,%d", col, row);
        }
    }
    if ( fixed ) {
        fputc('\n', stderr);
    }
    fclose(fp);
}

void
subtract(const char *fname) {
    FILE *fp;
    int dim[3] = {0, 0, 0};
    int comment = 0;
    int number = 0;
    int error = 0;
    int nd = 0;
    int c;
    int row;
    int col;
    unsigned short *pixel;

    if ( !(fp = fopen(fname, "rb")) ) {
        perror(fname);
        return;
    }
    if ( fgetc(fp) != 'P' || fgetc(fp) != '5' ) {
        error = 1;
    }
    while ( !error && nd < 3 && (c = fgetc(fp)) != EOF ) {
        if ( c == '#' ) {
            comment = 1;
        }
        if ( c == '\n' ) {
            comment = 0;
        }
        if ( comment ) {
            continue;
        }
        if ( isdigit(c) ) {
            number = 1;
        }
        if ( number ) {
            if ( isdigit(c) ) {
                dim[nd] = dim[nd] * 10 + c - '0';
            } else {
                if ( isspace(c)) {
                    number = 0;
                    nd++;
                } else {
                    error = 1;
                }
            }
        }
    }
    if ( error || nd < 3 ) {
        fprintf(stderr, _("%s is not a valid PGM file!\n"), fname);
        fclose(fp);
        return;
    } else {
        if ( dim[0] != width || dim[1] != height || dim[2] != 65535 ) {
            fprintf(stderr, _("%s has the wrong dimensions!\n"), fname);
            fclose(fp);
            return;
        }
    }
    pixel = (unsigned short *) calloc(width, sizeof *pixel);
    memoryError(pixel, "subtract()");
    for ( row = 0; row < height; row++ ) {
        fread(pixel, 2, width, fp);
        for ( col = 0; col < width; col++ ) {
            BAYER(row, col) = MAX (BAYER(row, col) - ntohs(pixel[col]), 0);
        }
    }
    free(pixel);
    fclose(fp);
    memset(cblack, 0, sizeof cblack);
    ADOBE_black = 0;
}

void
gamma_curve(double pwr, double ts, int mode, int imax) {
    int i;
    double g[6];
    double bnd[2] = {0, 0};
    double r;

    g[0] = pwr;
    g[1] = ts;
    g[2] = g[3] = g[4] = 0;
    bnd[g[1] >= 1] = 1;
    if ( g[1] && (g[1] - 1) * (g[0] - 1) <= 0 ) {
        for ( i = 0; i < 48; i++ ) {
            g[2] = (bnd[0] + bnd[1]) / 2;
            if ( g[0] ) {
                bnd[(pow(g[2] / g[1], -g[0]) - 1) / g[0] - 1 / g[2] > -1] = g[2];
            } else {
                bnd[g[2] / exp(1 - 1 / g[2]) < g[1]] = g[2];
            }
        }
        g[3] = g[2] / g[1];
        if ( g[0] ) {
            g[4] = g[2] * (1 / g[0] - 1);
        }
    }
    if ( g[0] ) {
        g[5] = 1 / (g[1] * SQR(g[3]) / 2 - g[4] * (1 - g[3]) +
                    (1 - pow(g[3], 1 + g[0])) * (1 + g[4]) / (1 + g[0])) - 1;
    } else {
        g[5] = 1 / (g[1] * SQR(g[3]) / 2 + 1
                    - g[2] - g[3] - g[2] * g[3] * (log(g[3]) - 1)) - 1;
    }
    if ( !mode-- ) {
        memcpy(OPTIONS_values->gammaParameters, g, sizeof OPTIONS_values->gammaParameters);
        return;
    }
    for ( i = 0; i < 0x10000; i++ ) {
        GAMMA_curveFunctionLookupTable[i] = 0xffff;
        if ( (r = (double) i / imax) < 1 ) {
            GAMMA_curveFunctionLookupTable[i] = 0x10000 * (mode
                                  ? (r < g[3] ? r * g[1] : (g[0] ? pow(r, g[0]) * (1 + g[4]) - g[4] : log(r) * g[2] +
                                                                                                      1))
                                  : (r < g[2] ? r / g[1] : (g[0] ? pow((r + g[4]) / (1 + g[4]), 1 / g[0]) : exp(
                            (r - 1) / g[2]))));
        }
    }
}

void
cam_xyz_coeff(float rgb_cam[3][4], double cam_xyz[4][3]) {
    double cam_rgb[4][3];
    double inverse[4][3];
    double num;
    int i;
    int j;
    int k;

    for ( i = 0; i < IMAGE_colors; i++ ) {
        // Multiply out XYZ colorspace
        for ( j = 0; j < 3; j++ ) {
            for ( cam_rgb[i][j] = k = 0; k < 3; k++ ) {
                cam_rgb[i][j] += cam_xyz[i][k] * xyz_rgb[k][j];
            }
        }
    }

    for ( i = 0; i < IMAGE_colors; i++ ) {
        // Normalize cam_rgb so that
        for ( num = j = 0; j < 3; j++ ) {
            // cam_rgb * (1,1,1) is (1,1,1,1)
            num += cam_rgb[i][j];
        }
        for ( j = 0; j < 3; j++ ) {
            cam_rgb[i][j] /= num;
        }
        pre_mul[i] = 1 / num;
    }
    pseudoinverse(cam_rgb, inverse, IMAGE_colors);
    for ( i = 0; i < 3; i++ ) {
        for ( j = 0; j < IMAGE_colors; j++ ) {
            rgb_cam[i][j] = inverse[j][i];
        }
    }
}

#ifdef COLORCHECK
void
colorcheck()
{
    #define NSQ 24
    // Coordinates of the GretagMacbeth ColorChecker squares
    // width, height, 1st_column, 1st_row
    int cut[NSQ][4];			// you must set these
    // ColorChecker Chart under 6500-kelvin illumination
    static const double gmb_xyY[NSQ][3] = {
        { 0.400, 0.350, 10.1 },		// Dark Skin
        { 0.377, 0.345, 35.8 },		// Light Skin
        { 0.247, 0.251, 19.3 },		// Blue Sky
        { 0.337, 0.422, 13.3 },		// Foliage
        { 0.265, 0.240, 24.3 },		// Blue Flower
        { 0.261, 0.343, 43.1 },		// Bluish Green
        { 0.506, 0.407, 30.1 },		// Orange
        { 0.211, 0.175, 12.0 },		// Purplish Blue
        { 0.453, 0.306, 19.8 },		// Moderate Red
        { 0.285, 0.202, 6.6  },		// Purple
        { 0.380, 0.489, 44.3 },		// Yellow Green
        { 0.473, 0.438, 43.1 },		// Orange Yellow
        { 0.187, 0.129, 6.1  },		// Blue
        { 0.305, 0.478, 23.4 },		// Green
        { 0.539, 0.313, 12.0 },		// Red
        { 0.448, 0.470, 59.1 },		// Yellow
        { 0.364, 0.233, 19.8 },		// Magenta
        { 0.196, 0.252, 19.8 },		// Cyan
        { 0.310, 0.316, 90.0 },		// White
        { 0.310, 0.316, 59.1 },		// Neutral 8
        { 0.310, 0.316, 36.2 },		// Neutral 6.5
        { 0.310, 0.316, 19.8 },		// Neutral 5
        { 0.310, 0.316, 9.0  },		// Neutral 3.5
        { 0.310, 0.316, 3.1  }      // Black
    };
    double gmb_cam[NSQ][4];
    double gmb_xyz[NSQ][3];
    double inverse[NSQ][3];
    double cam_xyz[4][3];
    double balance[4];
    double num;
    int c;
    int i;
    int j;
    int k;
    int sq;
    int row;
    int col;
    int pass;
    int count[4];

    memset (gmb_cam, 0, sizeof gmb_cam);
    for ( sq = 0; sq < NSQ; sq++ ) {
        for ( c = 0; IMAGE_colors < 3; c++ ) {
            count[c] = 0;
        }
        for ( row = cut[sq][3]; row < cut[sq][3]+cut[sq][1]; row++ ) {
            for ( col = cut[sq][2]; col < cut[sq][2] + cut[sq][0]; col++ ) {
                c = FC(row, col);
                if ( c >= IMAGE_colors ) c -= 2;
                gmb_cam[sq][c] += BAYER2(row, col);
                BAYER2(row, col) = ADOBE_black + (BAYER2(row, col) - ADOBE_black) / 2;
                count[c]++;
            }
        }
        for ( c = 0; c < IMAGE_colors; c++ ) {
            gmb_cam[sq][c] = gmb_cam[sq][c]/count[c] - ADOBE_black;
        }
        gmb_xyz[sq][0] = gmb_xyY[sq][2] * gmb_xyY[sq][0] / gmb_xyY[sq][1];
        gmb_xyz[sq][1] = gmb_xyY[sq][2];
        gmb_xyz[sq][2] = gmb_xyY[sq][2] * (1 - gmb_xyY[sq][0] - gmb_xyY[sq][1]) / gmb_xyY[sq][1];
    }
    pseudoinverse(gmb_xyz, inverse, NSQ);
    for ( pass = 0; pass < 2; pass++ ) {
        for ( GLOBAL_colorTransformForRaw = i=0; i < IMAGE_colors; i++ ) {
            for ( j = 0; j < 3; j++ ) {
                for ( cam_xyz[i][j] = k = 0; k < NSQ; k++ ) {
                    cam_xyz[i][j] += gmb_cam[k][i] * inverse[k][j];
                }
            }
            cam_xyz_coeff(rgb_cam, cam_xyz);
            for ( c = 0; c < IMAGE_colors; c++ ) {
                balance[c] = pre_mul[c] * gmb_cam[20][c];
            }
            for ( sq = 0; sq < NSQ; sq++ ) {
                for ( c = 0; c < IMAGE_colors; c++ ) {
                    gmb_cam[sq][c] *= balance[c];
                }
            }
        }
        if ( OPTIONS_values->verbose ) {
            printf ("    { \"%s %s\", %d,\n\t{", make, model, ADOBE_black);
            num = 10000 / (cam_xyz[1][0] + cam_xyz[1][1] + cam_xyz[1][2]);
            for ( c = 0; c < IMAGE_colors; c++ ) {
                for ( j = 0; j < 3; j++ ) {
                    printf("%c%d", (c | j) ? ',':' ', (int) (cam_xyz[c][j] * num + 0.5));
                }
            }
            puts(" } },");
        }
#undef NSQ
}
#endif

void
hat_transform(float *temp, float *base, int st, int size, int sc) {
    int i;

    for ( i = 0; i < sc; i++ ) {
        temp[i] = 2 * base[st * i] + base[st * (sc - i)] + base[st * (i + sc)];
    }
    for ( ; i + sc < size; i++ ) {
        temp[i] = 2 * base[st * i] + base[st * (i - sc)] + base[st * (i + sc)];
    }
    for ( ; i < size; i++ ) {
        temp[i] = 2 * base[st * i] + base[st * (i - sc)] + base[st * (2 * size - 2 - (i + sc))];
    }
}

void
wavelet_denoise() {
    float *fimg = 0;
    float *temp;
    float thold;
    float mul[2];
    float avg;
    float diff;
    int scale = 1;
    int size;
    int lev;
    int hpass;
    int lpass;
    int row;
    int col;
    int nc;
    int c;
    int i;
    int wlast;
    int blk[2];
    unsigned short *window[4];
    static const float noise[] =
            {0.8002, 0.2735, 0.1202, 0.0585, 0.0291, 0.0152, 0.0080, 0.0044};

    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, _("Wavelet denoising...\n"));
    }

    while ( ADOBE_maximum << scale < 0x10000 ) {
        scale++;
    }
    ADOBE_maximum <<= --scale;
    ADOBE_black <<= scale;
    for ( c = 0; c < 4; c++ ) {
        cblack[c] <<= scale;
    }
    if ((size = IMAGE_iheight * IMAGE_iwidth) < 0x15550000 ) {
        fimg = (float *) malloc((size * 3 + IMAGE_iheight + IMAGE_iwidth) * sizeof *fimg);
    }
    memoryError(fimg, "wavelet_denoise()");
    temp = fimg + size * 3;
    if ((nc = IMAGE_colors) == 3 && IMAGE_filters ) {
        nc++;
    }
    for ( c = 0; c < nc; c++ ) {
        // Denoise R,G1,B,G3 individually
        for ( i = 0; i < size; i++ ) {
            fimg[i] = 256 * sqrt(GLOBAL_image[i][c] << scale);
        }
        for ( hpass = lev = 0; lev < 5; lev++ ) {
            lpass = size * ((lev & 1) + 1);
            for ( row = 0; row < IMAGE_iheight; row++ ) {
                hat_transform(temp, fimg + hpass + row * IMAGE_iwidth, 1, IMAGE_iwidth, 1 << lev);
                for ( col = 0; col < IMAGE_iwidth; col++ ) {
                    fimg[lpass + row * IMAGE_iwidth + col] = temp[col] * 0.25;
                }
            }
            for ( col = 0; col < IMAGE_iwidth; col++ ) {
                hat_transform(temp, fimg + lpass + col, IMAGE_iwidth, IMAGE_iheight, 1 << lev);
                for ( row = 0; row < IMAGE_iheight; row++ ) {
                    fimg[lpass + row * IMAGE_iwidth + col] = temp[row] * 0.25;
                }
            }
            thold = OPTIONS_values->threshold * noise[lev];
            for ( i = 0; i < size; i++ ) {
                fimg[hpass + i] -= fimg[lpass + i];
                if ( fimg[hpass + i] < -thold ) {
                    fimg[hpass + i] += thold;
                } else {
                    if ( fimg[hpass + i] > thold ) {
                        fimg[hpass + i] -= thold;
                    } else {
                        fimg[hpass + i] = 0;
                    }
                }
                if ( hpass ) {
                    fimg[i] += fimg[hpass + i];
                }
            }
            hpass = lpass;
        }
        for ( i = 0; i < size; i++ ) {
            GLOBAL_image[i][c] = CLIP(SQR(fimg[i] + fimg[lpass + i]) / 0x10000);
        }
    }
    if ( IMAGE_filters && IMAGE_colors == 3 ) {
        // Pull G1 and G3 closer together
        for ( row = 0; row < 2; row++ ) {
            mul[row] = 0.125 * pre_mul[FC(row + 1, 0) | 1] / pre_mul[FC(row, 0) | 1];
            blk[row] = cblack[FC(row, 0) | 1];
        }
        for ( i = 0; i < 4; i++ ) {
            window[i] = (unsigned short *) fimg + width * i;
        }
        for ( wlast = -1, row = 1; row < height - 1; row++ ) {
            while ( wlast < row + 1 ) {
                for ( wlast++, i = 0; i < 4; i++ ) {
                    window[(i + 3) & 3] = window[i];
                }
                for ( col = FC(wlast, 1) & 1; col < width; col += 2 ) {
                    window[2][col] = BAYER(wlast, col);
                }
            }
            thold = OPTIONS_values->threshold / 512;
            for ( col = (FC(row, 0) & 1) + 1; col < width - 1; col += 2 ) {
                avg = (window[0][col - 1] + window[0][col + 1] +
                       window[2][col - 1] + window[2][col + 1] - blk[~row & 1] * 4)
                      * mul[row & 1] + (window[1][col] + blk[row & 1]) * 0.5;
                avg = avg < 0 ? 0 : sqrt(avg);
                diff = sqrt(BAYER(row, col)) - avg;
                if ( diff < -thold ) {
                    diff += thold;
                } else {
                    if ( diff > thold ) {
                        diff -= thold;
                    } else {
                        diff = 0;
                    }
                }
                BAYER(row, col) = CLIP(SQR(avg + diff) + 0.5);
            }
        }
    }
    free(fimg);
}

void
scale_colors() {
    unsigned bottom;
    unsigned right;
    unsigned size;
    unsigned row;
    unsigned col;
    unsigned ur;
    unsigned uc;
    unsigned i;
    unsigned x;
    unsigned y;
    unsigned c;
    unsigned sum[8];
    int val;
    int dark;
    int sat;
    double dsum[8];
    double dmin;
    double dmax;
    float scale_mul[4];
    float fr;
    float fc;
    unsigned short *img = 0;
    unsigned short *pix;

    if ( OPTIONS_values->userMul[0] ) {
        memcpy(pre_mul, OPTIONS_values->userMul, sizeof pre_mul);
    }

    if ( OPTIONS_values->useAutoWb || (OPTIONS_values->useCameraWb && GLOBAL_cam_mul[0] == -1)) {
        memset(dsum, 0, sizeof dsum);
        bottom = MIN (OPTIONS_values->greyBox[1] + OPTIONS_values->greyBox[3], height);
        right = MIN (OPTIONS_values->greyBox[0] + OPTIONS_values->greyBox[2], width);
        for ( row = OPTIONS_values->greyBox[1]; row < bottom; row += 8 ) {
            for ( col = OPTIONS_values->greyBox[0]; col < right; col += 8 ) {
                memset(sum, 0, sizeof sum);
                for ( y = row; y < row + 8 && y < bottom; y++ ) {
                    for ( x = col; x < col + 8 && x < right; x++ ) {
                        for ( c = 0; c < 4; c++ ) {
                            if ( IMAGE_filters ) {
                                c = fcol(y, x);
                                val = BAYER2(y, x);
                            } else {
                                val = GLOBAL_image[y * width + x][c];
                            }
                            if ( val > ADOBE_maximum - 25 ) {
                                goto skip_block;
                            }
                            if ( (val -= cblack[c]) < 0 ) {
                                val = 0;
                            }
                            sum[c] += val;
                            sum[c + 4]++;
                            if ( IMAGE_filters ) {
                                break;
                            }
                        }
                    }
                }
                for ( c = 0; c < 8; c++ ) {
                    dsum[c] += sum[c];
                }
                skip_block:;
            }
        }
        for ( c = 0; c < 4; c++ ) {
            if ( dsum[c] ) {
                pre_mul[c] = dsum[c + 4] / dsum[c];
            }
        }
    }
    if ( OPTIONS_values->useCameraWb && GLOBAL_cam_mul[0] != -1 ) {
        memset(sum, 0, sizeof sum);
        for ( row = 0; row < 8; row++ )
            for ( col = 0; col < 8; col++ ) {
                c = FC(row, col);
                if ( (val = white[row][col] - cblack[c]) > 0 ) {
                    sum[c] += val;
                }
                sum[c + 4]++;
            }
        if ( sum[0] && sum[1] && sum[2] && sum[3] ) {
            for ( c = 0; c < 4; c++ ) {
                pre_mul[c] = (float) sum[c + 4] / sum[c];
            }
        } else {
            if ( GLOBAL_cam_mul[0] && GLOBAL_cam_mul[2] ) {
                memcpy(pre_mul, GLOBAL_cam_mul, sizeof pre_mul);
            } else {
                fprintf(stderr, _("%s: Cannot use camera white balance.\n"), CAMERA_IMAGE_information.inputFilename);
            }
        }
    }
    if ( pre_mul[1] == 0 ) {
        pre_mul[1] = 1;
    }
    if ( pre_mul[3] == 0 ) {
        pre_mul[3] = IMAGE_colors < 4 ? pre_mul[1] : 1;
    }
    dark = ADOBE_black;
    sat = ADOBE_maximum;
    if ( OPTIONS_values->threshold ) {
        wavelet_denoise();
    }
    ADOBE_maximum -= ADOBE_black;
    for ( dmin = DBL_MAX, dmax = c = 0; c < 4; c++ ) {
        if ( dmin > pre_mul[c] ) {
            dmin = pre_mul[c];
        }
        if ( dmax < pre_mul[c] ) {
            dmax = pre_mul[c];
        }
    }
    if ( !OPTIONS_values->highlight ) {
        dmax = dmin;
    }
    for ( c = 0; c < 4; c++ ) {
        scale_mul[c] = (pre_mul[c] /= dmax) * 65535.0 / ADOBE_maximum;
    }
    if ( OPTIONS_values->verbose ) {
        fprintf(stderr,
                _("Scaling with darkness %d, saturation %d, and\nmultipliers"), dark, sat);
        for ( c = 0; c < 3; c++ ) {
            fprintf(stderr, " %f", pre_mul[c]);
        }
        fputc('\n', stderr);
    }
    if ( IMAGE_filters > 1000 && (cblack[4] + 1) / 2 == 1 && (cblack[5] + 1) / 2 == 1 ) {
        for ( c = 0; c < 3; c++ ) {
            cblack[FC(c / 2, c % 2)] +=
                    cblack[6 + c / 2 % cblack[4] * cblack[5] + c % 2 % cblack[5]];
        }
        cblack[4] = cblack[5] = 0;
    }
    size = IMAGE_iheight * IMAGE_iwidth;
    for ( i = 0; i < size * 4; i++ ) {
        if ( !(val = ((unsigned short *) GLOBAL_image)[i]) ) {
            continue;
        }
        if ( cblack[4] && cblack[5] ) {
            val -= cblack[6 + i / 4 / IMAGE_iwidth % cblack[4] * cblack[5] +
                          i / 4 % IMAGE_iwidth % cblack[5]];
        }
        val -= cblack[i & 3];
        val *= scale_mul[i & 3];
        ((unsigned short *)GLOBAL_image)[i] = CLIP(val);
    }
    if ((OPTIONS_values->chromaticAberrationCorrection[0] != 1 ||
         OPTIONS_values->chromaticAberrationCorrection[2] != 1) && IMAGE_colors == 3 ) {
        if ( OPTIONS_values->verbose ) {
            fprintf(stderr, _("Correcting chromatic aberration...\n"));
        }
        for ( c = 0; c < 4; c += 2 ) {
            if ( OPTIONS_values->chromaticAberrationCorrection[c] == 1 ) {
                continue;
            }
            img = (unsigned short *)malloc(size * sizeof *img);
            memoryError(img, "scale_colors()");
            for ( i = 0; i < size; i++ ) {
                img[i] = GLOBAL_image[i][c];
            }
            for ( row = 0; row < IMAGE_iheight; row++ ) {
                ur = fr = (row - IMAGE_iheight * 0.5) * OPTIONS_values->chromaticAberrationCorrection[c] + IMAGE_iheight * 0.5;
                if ( ur > IMAGE_iheight - 2 ) {
                    continue;
                }
                fr -= ur;
                for ( col = 0; col < IMAGE_iwidth; col++ ) {
                    uc = fc = (col - IMAGE_iwidth * 0.5) * OPTIONS_values->chromaticAberrationCorrection[c] + IMAGE_iwidth * 0.5;
                    if ( uc > IMAGE_iwidth - 2 ) {
                        continue;
                    }
                    fc -= uc;
                    pix = img + ur * IMAGE_iwidth + uc;
                    GLOBAL_image[row * IMAGE_iwidth + col][c] =
                            (pix[0] * (1 - fc) + pix[1] * fc) * (1 - fr) +
                            (pix[IMAGE_iwidth] * (1 - fc) + pix[IMAGE_iwidth + 1] * fc) * fr;
                }
            }
            free(img);
        }
    }
}

void
pre_interpolate() {
    unsigned short (*img)[4];
    int row;
    int col;
    int c;

    if ( IMAGE_shrink ) {
        if ( OPTIONS_values->halfSizePreInterpolation ) {
            height = IMAGE_iheight;
            width = IMAGE_iwidth;
            if ( IMAGE_filters == 9 ) {
                for ( row = 0; row < 3; row++ ) {
                    for ( col = 1; col < 4; col++ ) {
                        if ( !(GLOBAL_image[row * width + col][0] | GLOBAL_image[row * width + col][2])) {
                            goto break2;
                        }
                    }
                }
                break2:
                for ( ; row < height; row += 3 ) {
                    for ( col = (col - 1) % 3 + 1; col < width - 1; col += 3 ) {
                        img = GLOBAL_image + row * width + col;
                        for ( c = 0; c < 3; c += 2 ) {
                            img[0][c] = (img[-1][c] + img[1][c]) >> 1;
                        }
                    }
                }
            }
        } else {
            img = (unsigned short (*)[4]) calloc(height, width * sizeof *img);
            memoryError(img, "pre_interpolate()");
            for ( row = 0; row < height; row++ ) {
                for ( col = 0; col < width; col++ ) {
                    c = fcol(row, col);
                    img[row * width + col][c] = GLOBAL_image[(row >> 1) * IMAGE_iwidth + (col >> 1)][c];
                }
            }
            free(GLOBAL_image);
            GLOBAL_image = img;
            IMAGE_shrink = 0;
        }
    }
    if ( IMAGE_filters > 1000 && IMAGE_colors == 3 ) {
        mix_green = OPTIONS_values->fourColorRgb ^ OPTIONS_values->halfSizePreInterpolation;
        if ( OPTIONS_values->fourColorRgb | OPTIONS_values->halfSizePreInterpolation ) {
            IMAGE_colors++;
        } else {
            for ( row = FC(1, 0) >> 1; row < height; row += 2 ) {
                for ( col = FC(row, 1) & 1; col < width; col += 2 ) {
                    GLOBAL_image[row * width + col][1] = GLOBAL_image[row * width + col][3];
                }
            }
            IMAGE_filters &= ~((IMAGE_filters & 0x55555555) << 1);
        }
    }
    if ( OPTIONS_values->halfSizePreInterpolation ) {
        IMAGE_filters = 0;
    }
}

void
border_interpolate(int border) {
    unsigned row;
    unsigned col;
    unsigned y;
    unsigned x;
    unsigned f;
    unsigned c;
    unsigned sum[8];

    for ( row = 0; row < height; row++ ) {
        for ( col = 0; col < width; col++ ) {
            if ( col == border && row >= border && row < height - border ) {
                col = width - border;
            }
            memset(sum, 0, sizeof sum);
            for ( y = row - 1; y != row + 2; y++ ) {
                for ( x = col - 1; x != col + 2; x++ ) {
                    if ( y < height && x < width ) {
                        f = fcol(y, x);
                        sum[f] += GLOBAL_image[y * width + x][f];
                        sum[f + 4]++;
                    }
                }
            }
            f = fcol(row, col);
            for ( c = 0; c < IMAGE_colors; c++ ) {
                if ( c != f && sum[c + 4] ) {
                    GLOBAL_image[row * width + col][c] = sum[c] / sum[c + 4];
                }
            }
        }
    }
}

void
lin_interpolate() {
    int code[16][16][32];
    int size = 16;
    int *ip;
    int sum[4];
    int f;
    int c;
    int i;
    int x;
    int y;
    int row;
    int col;
    int shift;
    int color;
    unsigned short *pix;

    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, _("Bilinear interpolation...\n"));
    }
    if ( IMAGE_filters == 9 ) {
        size = 6;
    }
    border_interpolate(1);
    for ( row = 0; row < size; row++ ) {
        for ( col = 0; col < size; col++ ) {
            ip = code[row][col] + 1;
            f = fcol(row, col);
            memset(sum, 0, sizeof sum);
            for ( y = -1; y <= 1; y++ ) {
                for ( x = -1; x <= 1; x++ ) {
                    shift = (y == 0) + (x == 0);
                    color = fcol(row + y, col + x);
                    if ( color == f ) continue;
                    *ip++ = (width * y + x) * 4 + color;
                    *ip++ = shift;
                    *ip++ = color;
                    sum[color] += 1 << shift;
                }
            }
            code[row][col][0] = (ip - code[row][col]) / 3;
            for ( c = 0; c < IMAGE_colors; c++ ) {
                if ( c != f ) {
                    *ip++ = c;
                    *ip++ = 256 / sum[c];
                }
            }
        }
    }
    for ( row = 1; row < height - 1; row++ ) {
        for ( col = 1; col < width - 1; col++ ) {
            pix = GLOBAL_image[row * width + col];
            ip = code[row % size][col % size];
            memset(sum, 0, sizeof sum);
            for ( i = *ip++; i--; ip += 3 ) {
                sum[ip[2]] += pix[ip[0]] << ip[1];
            }
            for ( i = IMAGE_colors; --i; ip += 2 ) {
                pix[ip[0]] = sum[ip[0]] * ip[1] >> 8;
            }
        }
    }
}

/*
This algorithm is officially called:

"Interpolation using a Threshold-based variable number of gradients"

described in http://scien.stanford.edu/pages/labsite/1999/psych221/projects/99/tingchen/algodep/vargra.html

I've extended the basic idea to work with non-Bayer filter arrays.
Gradients are numbered clockwise from NW=0 to W=7.
*/
void
vng_interpolate() {
    static const signed char *cp;
    static const signed char terms[] = {
            -2, -2, +0, -1, 0, 0x01, -2, -2, +0, +0, 1, 0x01, -2, -1, -1, +0, 0, 0x01,
            -2, -1, +0, -1, 0, 0x02, -2, -1, +0, +0, 0, 0x03, -2, -1, +0, +1, 1, 0x01,
            -2, +0, +0, -1, 0, 0x06, -2, +0, +0, +0, 1, 0x02, -2, +0, +0, +1, 0, 0x03,
            -2, +1, -1, +0, 0, 0x04, -2, +1, +0, -1, 1, 0x04, -2, +1, +0, +0, 0, 0x06,
            -2, +1, +0, +1, 0, 0x02, -2, +2, +0, +0, 1, 0x04, -2, +2, +0, +1, 0, 0x04,
            -1, -2, -1, +0, 0, 0x80, -1, -2, +0, -1, 0, 0x01, -1, -2, +1, -1, 0, 0x01,
            -1, -2, +1, +0, 1, 0x01, -1, -1, -1, +1, 0, 0x88, -1, -1, +1, -2, 0, 0x40,
            -1, -1, +1, -1, 0, 0x22, -1, -1, +1, +0, 0, 0x33, -1, -1, +1, +1, 1, 0x11,
            -1, +0, -1, +2, 0, 0x08, -1, +0, +0, -1, 0, 0x44, -1, +0, +0, +1, 0, 0x11,
            -1, +0, +1, -2, 1, 0x40, -1, +0, +1, -1, 0, 0x66, -1, +0, +1, +0, 1, 0x22,
            -1, +0, +1, +1, 0, 0x33, -1, +0, +1, +2, 1, 0x10, -1, +1, +1, -1, 1, 0x44,
            -1, +1, +1, +0, 0, 0x66, -1, +1, +1, +1, 0, 0x22, -1, +1, +1, +2, 0, 0x10,
            -1, +2, +0, +1, 0, 0x04, -1, +2, +1, +0, 1, 0x04, -1, +2, +1, +1, 0, 0x04,
            +0, -2, +0, +0, 1, 0x80, +0, -1, +0, +1, 1, 0x88, +0, -1, +1, -2, 0, 0x40,
            +0, -1, +1, +0, 0, 0x11, +0, -1, +2, -2, 0, 0x40, +0, -1, +2, -1, 0, 0x20,
            +0, -1, +2, +0, 0, 0x30, +0, -1, +2, +1, 1, 0x10, +0, +0, +0, +2, 1, 0x08,
            +0, +0, +2, -2, 1, 0x40, +0, +0, +2, -1, 0, 0x60, +0, +0, +2, +0, 1, 0x20,
            +0, +0, +2, +1, 0, 0x30, +0, +0, +2, +2, 1, 0x10, +0, +1, +1, +0, 0, 0x44,
            +0, +1, +1, +2, 0, 0x10, +0, +1, +2, -1, 1, 0x40, +0, +1, +2, +0, 0, 0x60,
            +0, +1, +2, +1, 0, 0x20, +0, +1, +2, +2, 0, 0x10, +1, -2, +1, +0, 0, 0x80,
            +1, -1, +1, +1, 0, 0x88, +1, +0, +1, +2, 0, 0x08, +1, +0, +2, -1, 0, 0x40,
            +1, +0, +2, +1, 0, 0x10
    };
    static const signed char chood[] = {-1, -1, -1, 0, -1, +1, 0, +1, +1, +1, +1, 0, +1, -1, 0, -1};
    unsigned short (*brow[5])[4];
    unsigned short *pix;
    int prow = 8;
    int pcol = 2;
    int *ip;
    int *code[16][16];
    int gval[8];
    int gmin;
    int gmax;
    int sum[4];
    int row;
    int col;
    int x;
    int y;
    int x1;
    int x2;
    int y1;
    int y2;
    int t;
    int weight;
    int grads;
    int color;
    int diag;
    int g;
    int diff;
    int thold;
    int num;
    int c;

    lin_interpolate();
    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, _("VNG interpolation...\n"));
    }

    if ( IMAGE_filters == 1 ) {
        prow = pcol = 16;
    }
    if ( IMAGE_filters == 9 ) {
        prow = pcol = 6;
    }
    ip = (int *)calloc(prow * pcol, 1280);
    memoryError(ip, "vng_interpolate()");
    for ( row = 0; row < prow; row++ ) {
        // Precalculate for VNG
        for ( col = 0; col < pcol; col++ ) {
            code[row][col] = ip;
            for ( cp = terms, t = 0; t < 64; t++ ) {
                y1 = *cp++;
                x1 = *cp++;
                y2 = *cp++;
                x2 = *cp++;
                weight = *cp++;
                grads = *cp++;
                color = fcol(row + y1, col + x1);
                if ( fcol(row + y2, col + x2) != color ) {
                    continue;
                }
                diag = (fcol(row, col + 1) == color && fcol(row + 1, col) == color) ? 2 : 1;
                if ( abs(y1 - y2) == diag && abs(x1 - x2) == diag ) {
                    continue;
                }
                *ip++ = (y1 * width + x1) * 4 + color;
                *ip++ = (y2 * width + x2) * 4 + color;
                *ip++ = weight;
                for ( g = 0; g < 8; g++ ) {
                    if ( grads & 1 << g ) {
                        *ip++ = g;
                    }
                }
                *ip++ = -1;
            }
            *ip++ = INT_MAX;
            for ( cp = chood, g = 0; g < 8; g++ ) {
                y = *cp++;
                x = *cp++;
                *ip++ = (y * width + x) * 4;
                color = fcol(row, col);
                if ( fcol(row + y, col + x) != color && fcol(row + y * 2, col + x * 2) == color ) {
                    *ip++ = (y * width + x) * 8 + color;
                } else {
                    *ip++ = 0;
                }
            }
        }
    }
    brow[4] = (unsigned short (*)[4]) calloc(width * 3, sizeof **brow);
    memoryError(brow[4], "vng_interpolate()");
    for ( row = 0; row < 3; row++ ) {
        brow[row] = brow[4] + row * width;
    }
    for ( row = 2; row < height - 2; row++ ) {
        // Do VNG interpolation
        for ( col = 2; col < width - 2; col++ ) {
            pix = GLOBAL_image[row * width + col];
            ip = code[row % prow][col % pcol];
            memset(gval, 0, sizeof gval);
            while ((g = ip[0]) != INT_MAX ) {
                // Calculate gradients
                diff = ABS(pix[g] - pix[ip[1]]) << ip[2];
                gval[ip[3]] += diff;
                ip += 5;
                if ( (g = ip[-1]) == -1 ) {
                    continue;
                }
                gval[g] += diff;
                while ( (g = *ip++) != -1 ) {
                    gval[g] += diff;
                }
            }
            ip++;
            gmin = gmax = gval[0]; // Choose a threshold
            for ( g = 1; g < 8; g++ ) {
                if ( gmin > gval[g] ) {
                    gmin = gval[g];
                }
                if ( gmax < gval[g] ) {
                    gmax = gval[g];
                }
            }
            if ( gmax == 0 ) {
                memcpy(brow[2][col], pix, sizeof *GLOBAL_image);
                continue;
            }
            thold = gmin + (gmax >> 1);
            memset(sum, 0, sizeof sum);
            color = fcol(row, col);
            for ( num = g = 0; g < 8; g++, ip += 2 ) {
                // Average the neighbors
                if ( gval[g] <= thold ) {
                    for ( c = 0; c < IMAGE_colors; c++ ) {
                        if ( c == color && ip[1] ) {
                            sum[c] += (pix[c] + pix[ip[1]]) >> 1;
                        } else {
                            sum[c] += pix[ip[0] + c];
                        }
                    }
                    num++;
                }
            }
            for ( c = 0; c < IMAGE_colors; c++ ) {
                // Save to buffer
                t = pix[color];
                if ( c != color ) {
                    t += (sum[c] - sum[color]) / num;
                }
                brow[2][col][c] = CLIP(t);
            }
        }
        if ( row > 3 ) {
            // Write buffer to GLOBAL_image
            memcpy(GLOBAL_image[(row - 2) * width + 2], brow[0] + 2, (width - 4) * sizeof *GLOBAL_image);
        }
        for ( g = 0; g < 4; g++ ) {
            brow[(g - 1) & 3] = brow[g];
        }
    }
    memcpy(GLOBAL_image[(row - 2) * width + 2], brow[0] + 2, (width - 4) * sizeof *GLOBAL_image);
    memcpy(GLOBAL_image[(row - 1) * width + 2], brow[1] + 2, (width - 4) * sizeof *GLOBAL_image);
    free(brow[4]);
    free(code[0][0]);
}

/*
Patterned Pixel Grouping Interpolation by Alain Desbiolles
*/
void
ppg_interpolate() {
    int dir[5] = {1, width, -1, -width, 1};
    int row;
    int col;
    int diff[2];
    int guess[2];
    int c;
    int d;
    int i;
    unsigned short (*pix)[4];

    border_interpolate(3);
    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, _("PPG interpolation...\n"));
    }

    // Fill in the green layer with gradients and pattern recognition:
    for ( row = 3; row < height - 3; row++ ) {
        for ( col = 3 + (FC(row, 3) & 1), c = FC(row, col); col < width - 3; col += 2 ) {
            pix = GLOBAL_image + row * width + col;
            for ( i = 0; (d = dir[i]) > 0; i++ ) {
                guess[i] = (pix[-d][1] + pix[0][c] + pix[d][1]) * 2
                           - pix[-2 * d][c] - pix[2 * d][c];
                diff[i] = (ABS(pix[-2 * d][c] - pix[0][c]) +
                           ABS(pix[2 * d][c] - pix[0][c]) +
                           ABS(pix[-d][1] - pix[d][1])) * 3 +
                          (ABS(pix[3 * d][1] - pix[d][1]) +
                           ABS(pix[-3 * d][1] - pix[-d][1])) * 2;
            }
            d = dir[i = diff[0] > diff[1]];
            pix[0][1] = ULIM(guess[i] >> 2, pix[d][1], pix[-d][1]);
        }
    }

    // Calculate red and blue for each green pixel:
    for ( row = 1; row < height - 1; row++ ) {
        for ( col = 1 + (FC(row, 2) & 1), c = FC(row, col + 1); col < width - 1; col += 2 ) {
            pix = GLOBAL_image + row * width + col;
            for ( i = 0; (d = dir[i]) > 0; c = 2 - c, i++ ) {
                pix[0][c] = CLIP((pix[-d][c] + pix[d][c] + 2 * pix[0][1]
                                  - pix[-d][1] - pix[d][1]) >> 1);
            }
        }
    }

    // Calculate blue for red pixels and vice versa:
    for ( row = 1; row < height - 1; row++ ) {
        for ( col = 1 + (FC(row, 1) & 1), c = 2 - FC(row, col); col < width - 1; col += 2 ) {
            pix = GLOBAL_image + row * width + col;
            for ( i = 0; (d = dir[i] + dir[i + 1]) > 0; i++ ) {
                diff[i] = ABS(pix[-d][c] - pix[d][c]) +
                          ABS(pix[-d][1] - pix[0][1]) +
                          ABS(pix[d][1] - pix[0][1]);
                guess[i] = pix[-d][c] + pix[d][c] + 2 * pix[0][1]
                           - pix[-d][1] - pix[d][1];
            }
            if ( diff[0] != diff[1] ) {
                pix[0][c] = CLIP(guess[diff[0] > diff[1]] >> 1);
            } else {
                pix[0][c] = CLIP((guess[0] + guess[1]) >> 2);
            }
        }
    }
}

void
cielab(unsigned short rgb[3], short lab[3]) {
    int c;
    int i;
    int j;
    int k;
    float r;
    float xyz[3];
    static float cbrt[0x10000];
    static float xyz_cam[3][4];

    if ( !rgb ) {
        for ( i = 0; i < 0x10000; i++ ) {
            r = i / 65535.0;
            cbrt[i] = r > 0.008856 ? pow(r, 1 / 3.0) : 7.787 * r + 16 / 116.0;
        }
        for ( i = 0; i < 3; i++ ) {
            for ( j = 0; j < IMAGE_colors; j++ ) {
                for ( xyz_cam[i][j] = k = 0; k < 3; k++ ) {
                    xyz_cam[i][j] += xyz_rgb[i][k] * rgb_cam[k][j] / d65_white[i];
                }
            }
        }
        return;
    }
    xyz[0] = xyz[1] = xyz[2] = 0.5;
    for ( c = 0; c < IMAGE_colors; c++ ) {
        xyz[0] += xyz_cam[0][c] * rgb[c];
        xyz[1] += xyz_cam[1][c] * rgb[c];
        xyz[2] += xyz_cam[2][c] * rgb[c];
    }
    xyz[0] = cbrt[CLIP((int) xyz[0])];
    xyz[1] = cbrt[CLIP((int) xyz[1])];
    xyz[2] = cbrt[CLIP((int) xyz[2])];
    lab[0] = 64 * (116 * xyz[1] - 16);
    lab[1] = 64 * 500 * (xyz[0] - xyz[1]);
    lab[2] = 64 * 200 * (xyz[1] - xyz[2]);
}

#define TS 512        /* Tile Size */
#define fcol(row, col) xtrans[(row+6) % 6][(col+6) % 6]

/*
Frank Markesteijn's algorithm for Fuji X-Trans sensors
*/
void
xtrans_interpolate(int passes) {
    int c;
    int d;
    int f;
    int g;
    int h;
    int i;
    int v;
    int ng;
    int row;
    int col;
    int top;
    int left;
    int mrow;
    int mcol;
    int val;
    int ndir;
    int pass;
    int hm[8];
    int avg[4];
    int color[3][8];
    static const short orth[12] = {1, 0, 0, 1, -1, 0, 0, -1, 1, 0, 0, 1};
    static const short patt[2][16] = {{0, 1, 0, -1, 2, 0, -1, 0, 1, 1, 1,  -1, 0, 0,  0,  0},
                           {0, 1, 0, -2, 1, 0, -2, 0, 1, 1, -2, -2, 1, -1, -1, 1}};
    static const short dir[4] = {1, TS, TS + 1, TS - 1};
    short allhex[3][3][2][8];
    short *hex;
    unsigned short min;
    unsigned short max;
    unsigned short sgrow;
    unsigned short sgcol;
    unsigned short (*rgb)[TS][TS][3];
    unsigned short (*rix)[3];
    unsigned short (*pix)[4];
    short (*lab)[TS][3];
    short (*lix)[3];
    float (*drv)[TS][TS];
    float diff[6];
    float tr;
    char (*homo)[TS][TS];
    char *buffer;

    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, _("%d-pass X-Trans interpolation...\n"), passes);
    }

    cielab(0, 0);
    ndir = 4 << (passes > 1);
    buffer = (char *) malloc(TS * TS * (ndir * 11 + 6));
    memoryError(buffer, "xtrans_interpolate()");
    rgb = (unsigned short (*)[TS][TS][3]) buffer;
    lab = (short (*)[TS][3]) (buffer + TS * TS * (ndir * 6));
    drv = (float (*)[TS][TS]) (buffer + TS * TS * (ndir * 6 + 6));
    homo = (char (*)[TS][TS]) (buffer + TS * TS * (ndir * 10 + 6));

    // Map a green hexagon around each non-green pixel and vice versa:
    for ( row = 0; row < 3; row++ ) {
        for ( col = 0; col < 3; col++ ) {
            for ( ng = d = 0; d < 10; d += 2 ) {
                g = fcol(row, col) == 1;
                if ( fcol(row + orth[d], col + orth[d + 2]) == 1 ) {
                    ng = 0;
                } else {
                    ng++;
                }
                if ( ng == 4 ) {
                    sgrow = row;
                    sgcol = col;
                }
                if ( ng == g + 1 )
                    for ( c = 0; c < 8; c++ ) {
                        v = orth[d] * patt[g][c * 2] + orth[d + 1] * patt[g][c * 2 + 1];
                        h = orth[d + 2] * patt[g][c * 2] + orth[d + 3] * patt[g][c * 2 + 1];
                        allhex[row][col][0][c ^ (g * 2 & d)] = h + v * width;
                        allhex[row][col][1][c ^ (g * 2 & d)] = h + v * TS;
                    }
            }
        }
    }

    // Set green1 and green3 to the minimum and ADOBE_maximum allowed values:
    for ( row = 2; row < height - 2; row++ ) {
        for ( min = ~(max = 0), col = 2; col < width - 2; col++ ) {
            if ( fcol(row, col) == 1 && (min = ~(max = 0))) continue;
            pix = GLOBAL_image + row * width + col;
            hex = allhex[row % 3][col % 3][0];
            if ( !max ) {
                for ( c = 0; c < 6; c++ ) {
                    val = pix[hex[c]][1];
                    if ( min > val ) {
                        min = val;
                    }
                    if ( max < val ) {
                        max = val;
                    }
                }
            }
            pix[0][1] = min;
            pix[0][3] = max;
            switch ( (row - sgrow) % 3 ) {
                case 1:
                    if ( row < height - 3 ) {
                        row++;
                        col--;
                    }
                    break;
                case 2:
                    if ( (min = ~(max = 0)) && (col += 2) < width - 3 && row > 2 ) {
                        row--;
                    }
            }
        }
    }

    for ( top = 3; top < height - 19; top += TS - 16 ) {
        for ( left = 3; left < width - 19; left += TS - 16 ) {
            mrow = MIN (top + TS, height - 3);
            mcol = MIN (left + TS, width - 3);
            for ( row = top; row < mrow; row++ ) {
                for ( col = left; col < mcol; col++ ) {
                    memcpy(rgb[0][row - top][col - left], GLOBAL_image[row * width + col], 6);
                }
            }
            for ( c = 0; c < 3; c++ ) {
                memcpy(rgb[c + 1], rgb[0], sizeof *rgb);
            };

            // Interpolate green horizontally, vertically, and along both diagonals:
            for ( row = top; row < mrow; row++ ) {
                for ( col = left; col < mcol; col++ ) {
                    if ( (f = fcol(row, col)) == 1 ) {
                        continue;
                    }
                    pix = GLOBAL_image + row * width + col;
                    hex = allhex[row % 3][col % 3][0];
                    color[1][0] = 174 * (pix[hex[1]][1] + pix[hex[0]][1]) -
                                  46 * (pix[2 * hex[1]][1] + pix[2 * hex[0]][1]);
                    color[1][1] = 223 * pix[hex[3]][1] + pix[hex[2]][1] * 33 +
                                  92 * (pix[0][f] - pix[-hex[2]][f]);
                    for ( c = 0; c < 2; c++ ) {
                        color[1][2 + c] =
                                164 * pix[hex[4 + c]][1] + 92 * pix[-2 * hex[4 + c]][1] + 33 *
                                                                                          (2 * pix[0][f] -
                                                                                           pix[3 * hex[4 + c]][f] -
                                                                                           pix[-3 * hex[4 + c]][f]);
                    }
                    for ( c = 0; c < 4; c++ ) {
                        rgb[c ^ !((row - sgrow) % 3)][row - top][col - left][1] =
                                LIM(color[1][c] >> 8, pix[0][1], pix[0][3]);
                    }
                }
            }

            for ( pass = 0; pass < passes; pass++ ) {
                if ( pass == 1 ) {
                    memcpy(rgb += 4, buffer, 4 * sizeof *rgb);
                }

                // Recalculate green from interpolated values of closer pixels:
                if ( pass ) {
                    for ( row = top + 2; row < mrow - 2; row++ ) {
                        for ( col = left + 2; col < mcol - 2; col++ ) {
                            if ((f = fcol(row, col)) == 1 ) continue;
                            pix = GLOBAL_image + row * width + col;
                            hex = allhex[row % 3][col % 3][1];
                            for ( d = 3; d < 6; d++ ) {
                                rix = &rgb[(d - 2) ^ !((row - sgrow) % 3)][row - top][col - left];
                                val = rix[-2 * hex[d]][1] + 2 * rix[hex[d]][1]
                                      - rix[-2 * hex[d]][f] - 2 * rix[hex[d]][f] + 3 * rix[0][f];
                                rix[0][1] = LIM(val / 3, pix[0][1], pix[0][3]);
                            }
                        }
                    }
                }

                // Interpolate red and blue values for solitary green pixels:
                for ( row = (top - sgrow + 4) / 3 * 3 + sgrow; row < mrow - 2; row += 3 )
                    for ( col = (left - sgcol + 4) / 3 * 3 + sgcol; col < mcol - 2; col += 3 ) {
                        rix = &rgb[0][row - top][col - left];
                        h = fcol(row, col + 1);
                        memset(diff, 0, sizeof diff);
                        for ( i = 1, d = 0; d < 6; d++, i ^= TS ^ 1, h ^= 2 ) {
                            for ( c = 0; c < 2; c++, h ^= 2 ) {
                                g = 2 * rix[0][1] - rix[i << c][1] - rix[-i << c][1];
                                color[h][d] = g + rix[i << c][h] + rix[-i << c][h];
                                if ( d > 1 ) {
                                    diff[d] += SQR (rix[i << c][1] - rix[-i << c][1]
                                                    - rix[i << c][h] + rix[-i << c][h]) + SQR(g);
                                }
                            }
                            if ( d > 1 && (d & 1) ) {
                                if ( diff[d - 1] < diff[d] ) {
                                    for ( c = 0; c < 2; c++ ) {
                                        color[c * 2][d] = color[c * 2][d - 1];
                                    }
                                }
                            }
                            if ( d < 2 || (d & 1) ) {
                                for ( c = 0; c < 2; c++ ) {
                                    rix[0][c * 2] = CLIP(color[c * 2][d] / 2);
                                }
                                rix += TS * TS;
                            }
                        }
                    }

                // Interpolate red for blue pixels and vice versa:
                for ( row = top + 3; row < mrow - 3; row++ )
                    for ( col = left + 3; col < mcol - 3; col++ ) {
                        if ( (f = 2 - fcol(row, col)) == 1 ) {
                            continue;
                        }
                        rix = &rgb[0][row - top][col - left];
                        c = (row - sgrow) % 3 ? TS : 1;
                        h = 3 * (c ^ TS ^ 1);
                        for ( d = 0; d < 4; d++, rix += TS * TS ) {
                            i = d > 1 || ((d ^ c) & 1) ||
                                ((ABS(rix[0][1] - rix[c][1]) + ABS(rix[0][1] - rix[-c][1])) <
                                 2 * (ABS(rix[0][1] - rix[h][1]) + ABS(rix[0][1] - rix[-h][1]))) ? c : h;
                            rix[0][f] = CLIP((rix[i][f] + rix[-i][f] +
                                              2 * rix[0][1] - rix[i][1] - rix[-i][1]) / 2);
                        }
                    }

                // Fill in red and blue for 2x2 blocks of green:
                for ( row = top + 2; row < mrow - 2; row++ ) {
                    if ((row - sgrow) % 3 ) {
                        for ( col = left + 2; col < mcol - 2; col++ ) {
                            if ((col - sgcol) % 3 ) {
                                rix = &rgb[0][row - top][col - left];
                                hex = allhex[row % 3][col % 3][1];
                                for ( d = 0; d < ndir; d += 2, rix += TS * TS ) {
                                    if ( hex[d] + hex[d + 1] ) {
                                        g = 3 * rix[0][1] - 2 * rix[hex[d]][1] - rix[hex[d + 1]][1];
                                        for ( c = 0; c < 4; c += 2 ) {
                                            rix[0][c] =
                                                    CLIP((g + 2 * rix[hex[d]][c] + rix[hex[d + 1]][c]) / 3);
                                        }
                                    } else {
                                        g = 2 * rix[0][1] - rix[hex[d]][1] - rix[hex[d + 1]][1];
                                        for ( c = 0; c < 4; c += 2 ) {
                                            rix[0][c] =
                                                    CLIP((g + rix[hex[d]][c] + rix[hex[d + 1]][c]) / 2);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            rgb = (unsigned short (*)[TS][TS][3]) buffer;
            mrow -= top;
            mcol -= left;

            // Convert to CIELab and differentiate in all directions:
            for ( d = 0; d < ndir; d++ ) {
                for ( row = 2; row < mrow - 2; row++ ) {
                    for ( col = 2; col < mcol - 2; col++ ) {
                        cielab(rgb[d][row][col], lab[row][col]);
                    }
                }
                for ( f = dir[d & 3], row = 3; row < mrow - 3; row++ ) {
                    for ( col = 3; col < mcol - 3; col++ ) {
                        lix = &lab[row][col];
                        g = 2 * lix[0][0] - lix[f][0] - lix[-f][0];
                        drv[d][row][col] = SQR(g)
                                           + SQR((2 * lix[0][1] - lix[f][1] - lix[-f][1] + g * 500.0 / 232.0))
                                           + SQR((2 * lix[0][2] - lix[f][2] - lix[-f][2] - g * 500.0 / 580.0));
                    }
                }
            }

            // Build homogeneity maps from the derivatives:
            memset(homo, 0, ndir * TS * TS);
            for ( row = 4; row < mrow - 4; row++ ) {
                for ( col = 4; col < mcol - 4; col++ ) {
                    for ( tr = FLT_MAX, d = 0; d < ndir; d++ ) {
                        if ( tr > drv[d][row][col] ) {
                            tr = drv[d][row][col];
                        }
                    }
                    tr *= 8;
                    for ( d = 0; d < ndir; d++ ) {
                        for ( v = -1; v <= 1; v++ ) {
                            for ( h = -1; h <= 1; h++ ) {
                                if ( drv[d][row + v][col + h] <= tr ) {
                                    homo[d][row][col]++;
                                }
                            }
                        }
                    }
                }
            }

            // Average the most homogenous pixels for the final result:
            if ( height - top < TS + 4 ) {
                mrow = height - top + 2;
            }
            if ( width - left < TS + 4 ) {
                mcol = width - left + 2;
            }
            for ( row = MIN(top, 8); row < mrow - 8; row++ ) {
                for ( col = MIN(left, 8); col < mcol - 8; col++ ) {
                    for ( d = 0; d < ndir; d++ ) {
                        for ( hm[d] = 0, v = -2; v <= 2; v++ ) {
                            for ( h = -2; h <= 2; h++ ) {
                                hm[d] += homo[d][row + v][col + h];
                            }
                        }
                    }

                    for ( d = 0; d < ndir - 4; d++ ) {
                        if ( hm[d] < hm[d + 4] ) {
                            hm[d] = 0;
                        } else if ( hm[d] > hm[d + 4] ) {
                            hm[d + 4] = 0;
                        }
                    }
                    for ( max = hm[0], d = 1; d < ndir; d++ ) {
                        if ( max < hm[d] ) {
                            max = hm[d];
                        }
                    }
                    max -= max >> 3;
                    memset(avg, 0, sizeof avg);
                    for ( d = 0; d < ndir; d++ ) {
                        if ( hm[d] >= max ) {
                            for ( c = 0; c < 3; c++ ) {
                                avg[c] += rgb[d][row][col][c];
                            }
                            avg[3]++;
                        }
                    }
                    for ( c = 0; c < 3; c++ ) {
                        GLOBAL_image[(row + top) * width + col + left][c] = avg[c] / avg[3];
                    }
                }
            }
        }
    }
    free(buffer);
    border_interpolate(8);
}

#undef fcol

/*
Adaptive Homogeneity-Directed interpolation is based on
the work of Keigo Hirakawa, Thomas Parks, and Paul Lee.
*/
void
ahd_interpolate() {
    int i;
    int j;
    int top;
    int left;
    int row;
    int col;
    int tr;
    int tc;
    int c;
    int d;
    int val;
    int hm[2];
    static const int dir[4] = {-1, 1, -TS, TS};
    unsigned ldiff[2][4];
    unsigned abdiff[2][4];
    unsigned leps;
    unsigned abeps;
    unsigned short (*rgb)[TS][TS][3];
    unsigned short (*rix)[3];
    unsigned short (*pix)[4];
    short (*lab)[TS][TS][3];
    short (*lix)[3];
    char (*homo)[TS][TS];
    char *buffer;

    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, _("AHD interpolation...\n"));
    }

    cielab(0, 0);
    border_interpolate(5);
    buffer = (char *)malloc(26 * TS * TS);
    memoryError(buffer, "ahd_interpolate()");
    rgb = (unsigned short (*)[TS][TS][3]) buffer;
    lab = (short (*)[TS][TS][3]) (buffer + 12 * TS * TS);
    homo = (char (*)[TS][TS]) (buffer + 24 * TS * TS);

    for ( top = 2; top < height - 5; top += TS - 6 ) {
        for ( left = 2; left < width - 5; left += TS - 6 ) {
            // Interpolate green horizontally and vertically:
            for ( row = top; row < top + TS && row < height - 2; row++ ) {
                col = left + (FC(row, left) & 1);
                for ( c = FC(row, col); col < left + TS && col < width - 2; col += 2 ) {
                    pix = GLOBAL_image + row * width + col;
                    val = ((pix[-1][1] + pix[0][c] + pix[1][1]) * 2
                           - pix[-2][c] - pix[2][c]) >> 2;
                    rgb[0][row - top][col - left][1] = ULIM(val, pix[-1][1], pix[1][1]);
                    val = ((pix[-width][1] + pix[0][c] + pix[width][1]) * 2
                           - pix[-2 * width][c] - pix[2 * width][c]) >> 2;
                    rgb[1][row - top][col - left][1] = ULIM(val, pix[-width][1], pix[width][1]);
                }
            }

            // Interpolate red and blue, and convert to CIELab:
            for ( d = 0; d < 2; d++ ) {
                for ( row = top + 1; row < top + TS - 1 && row < height - 3; row++ ) {
                    for ( col = left + 1; col < left + TS - 1 && col < width - 3; col++ ) {
                        pix = GLOBAL_image + row * width + col;
                        rix = &rgb[d][row - top][col - left];
                        lix = &lab[d][row - top][col - left];
                        if ( (c = 2 - FC(row, col)) == 1 ) {
                            c = FC(row + 1, col);
                            val = pix[0][1] + ((pix[-1][2 - c] + pix[1][2 - c]
                                                - rix[-1][1] - rix[1][1]) >> 1);
                            rix[0][2 - c] = CLIP(val);
                            val = pix[0][1] + ((pix[-width][c] + pix[width][c]
                                                - rix[-TS][1] - rix[TS][1]) >> 1);
                        } else
                            val = rix[0][1] + ((pix[-width - 1][c] + pix[-width + 1][c]
                                                + pix[+width - 1][c] + pix[+width + 1][c]
                                                - rix[-TS - 1][1] - rix[-TS + 1][1]
                                                - rix[+TS - 1][1] - rix[+TS + 1][1] + 1) >> 2);
                        rix[0][c] = CLIP(val);
                        c = FC(row, col);
                        rix[0][c] = pix[0][c];
                        cielab(rix[0], lix[0]);
                    }
                }
            }

            // Build homogeneity maps from the CIELab images:
            memset(homo, 0, 2 * TS * TS);
            for ( row = top + 2; row < top + TS - 2 && row < height - 4; row++ ) {
                tr = row - top;
                for ( col = left + 2; col < left + TS - 2 && col < width - 4; col++ ) {
                    tc = col - left;
                    for ( d = 0; d < 2; d++ ) {
                        lix = &lab[d][tr][tc];
                        for ( i = 0; i < 4; i++ ) {
                            ldiff[d][i] = ABS(lix[0][0] - lix[dir[i]][0]);
                            abdiff[d][i] = SQR(lix[0][1] - lix[dir[i]][1])
                                           + SQR(lix[0][2] - lix[dir[i]][2]);
                        }
                    }
                    leps = MIN(MAX(ldiff[0][0], ldiff[0][1]),
                               MAX(ldiff[1][2], ldiff[1][3]));
                    abeps = MIN(MAX(abdiff[0][0], abdiff[0][1]),
                                MAX(abdiff[1][2], abdiff[1][3]));
                    for ( d = 0; d < 2; d++ ) {
                        for ( i = 0; i < 4; i++ ) {
                            if ( ldiff[d][i] <= leps && abdiff[d][i] <= abeps ) {
                                homo[d][tr][tc]++;
                            }
                        }
                    }
                }
            }

            // Combine the most homogenous pixels for the final result:
            for ( row = top + 3; row < top + TS - 3 && row < height - 5; row++ ) {
                tr = row - top;
                for ( col = left + 3; col < left + TS - 3 && col < width - 5; col++ ) {
                    tc = col - left;
                    for ( d = 0; d < 2; d++ ) {
                        for ( hm[d] = 0, i = tr - 1; i <= tr + 1; i++ ) {
                            for ( j = tc - 1; j <= tc + 1; j++ ) {
                                hm[d] += homo[d][i][j];
                            }
                        }
                    }
                    if ( hm[0] != hm[1] ) {
                        for ( c = 0; c < 3; c++ ) {
                            GLOBAL_image[row * width + col][c] = rgb[hm[1] > hm[0]][tr][tc][c];
                        }
                    } else {
                        for ( c = 0; c < 3; c++ ) {
                            GLOBAL_image[row * width + col][c] = (rgb[0][tr][tc][c] + rgb[1][tr][tc][c]) >> 1;
                        }
                    }
                }
            }
        }
    }
    free(buffer);
}

#undef TS

void
median_filter() {
    unsigned short (*pix)[4];
    int pass;
    int c;
    int i;
    int j;
    int k;
    int med[9];
    static const unsigned char opt[] =    /* Optimal 9-element median search */
            {1, 2, 4, 5, 7, 8, 0, 1, 3, 4, 6, 7, 1, 2, 4, 5, 7, 8,
             0, 3, 5, 8, 4, 7, 3, 6, 1, 4, 2, 5, 4, 7, 4, 2, 6, 4, 4, 2};

    for ( pass = 1; pass <= OPTIONS_values->med_passes; pass++ ) {
        if ( OPTIONS_values->verbose ) {
            fprintf(stderr, _("Median filter pass %d...\n"), pass);
        }
        for ( c = 0; c < 3; c += 2 ) {
            for ( pix = GLOBAL_image; pix < GLOBAL_image + width * height; pix++ ) {
                pix[0][3] = pix[0][c];
            }
            for ( pix = GLOBAL_image + width; pix < GLOBAL_image + width * (height - 1); pix++ ) {
                if ((pix - GLOBAL_image + 1) % width < 2 ) {
                    continue;
                }
                for ( k = 0, i = -width; i <= width; i += width ) {
                    for ( j = i - 1; j <= i + 1; j++ ) {
                        med[k++] = pix[j][3] - pix[j][1];
                    }
                }
                for ( i = 0; i < sizeof opt; i += 2 ) {
                    if ( med[opt[i]] > med[opt[i + 1]] ) {
                        SWAP (med[opt[i]], med[opt[i + 1]]);
                    }
                }
                pix[0][c] = CLIP(med[4] + pix[0][1]);
            }
        }
    }
}

void
blend_highlights() {
    int clip = INT_MAX;
    int row;
    int col;
    int c;
    int i;
    int j;
    static const float trans[2][4][4] =
            {{{1, 1, 1},    {1.7320508, -1.7320508, 0},     {-1, -1, 2}},
             {{1, 1, 1, 1}, {1,         -1,         1, -1}, {1,  1,  -1, -1}, {1, -1, -1, 1}}};
    static const float itrans[2][4][4] =
            {{{1, 0.8660254, -0.5}, {1, -0.8660254, -0.5},  {1, 0, 1}},
             {{1, 1,         1, 1}, {1, -1,         1, -1}, {1, 1, -1, -1}, {1, -1, -1, 1}}};
    float cam[2][4];
    float lab[2][4];
    float sum[2];
    float chratio;

    if ((unsigned) (IMAGE_colors - 3) > 1 ) {
        return;
    }
    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, _("Blending highlights...\n"));
    }
    for ( c = 0; c < IMAGE_colors; c++ ) {
        if ( clip > (i = 65535 * pre_mul[c]) ) {
            clip = i;
        }
    }
    for ( row = 0; row < height; row++ ) {
        for ( col = 0; col < width; col++ ) {
            for ( c = 0; c < IMAGE_colors; c++ ) {
                if ( GLOBAL_image[row * width + col][c] > clip ) {
                    break;
                }
            }
            if ( c == IMAGE_colors ) {
                continue;
            }
            for ( c = 0; c < IMAGE_colors; c++ ) {
                cam[0][c] = GLOBAL_image[row * width + col][c];
                cam[1][c] = MIN(cam[0][c], clip);
            }
            for ( i = 0; i < 2; i++ ) {
                for ( c = 0; c < IMAGE_colors; c++ ) {
                    for ( lab[i][c] = j = 0; j < IMAGE_colors; j++ ) {
                        lab[i][c] += trans[IMAGE_colors - 3][c][j] * cam[i][j];
                    }
                }
                for ( sum[i] = 0, c = 1; c < IMAGE_colors; c++ ) {}
                sum[i] += SQR(lab[i][c]);
            }
            chratio = sqrt(sum[1] / sum[0]);
            for ( c = 1; c < IMAGE_colors; c++ ) {
                lab[0][c] *= chratio;
            }
            for ( c = 0; c < IMAGE_colors; c++ ) {
                for ( cam[0][c] = j = 0; j < IMAGE_colors; j++ ) {
                    cam[0][c] += itrans[IMAGE_colors - 3][c][j] * lab[0][j];
                }
            }
            for ( c = 0; c < IMAGE_colors; c++ ) {
                GLOBAL_image[row * width + col][c] = cam[0][c] / IMAGE_colors;
            }
        }
    }
}

#define SCALE (4 >> IMAGE_shrink)

void
recover_highlights() {
    float *map;
    float sum;
    float wgt;
    float grow;
    int hsat[4];
    int count;
    int spread;
    int change;
    int val;
    int i;
    unsigned high;
    unsigned wide;
    unsigned mrow;
    unsigned mcol;
    unsigned row;
    unsigned col;
    unsigned kc;
    unsigned c;
    unsigned d;
    unsigned y;
    unsigned x;
    unsigned short *pixel;
    static const signed char dir[8][2] =
            {{-1, -1},
             {-1, 0},
             {-1, 1},
             {0,  1},
             {1,  1},
             {1,  0},
             {1,  -1},
             {0,  -1}};

    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, _("Rebuilding highlights...\n"));
    }

    grow = pow(2, 4 - OPTIONS_values->highlight);
    for ( c = 0; c < IMAGE_colors; c++ ) {
        hsat[c] = 32000 * pre_mul[c];
    }
    for ( kc = 0, c = 1; c < IMAGE_colors; c++ ) {
        if ( pre_mul[kc] < pre_mul[c] ) kc = c;
    }
    high = height / SCALE;
    wide = width / SCALE;
    map = (float *)calloc(high, wide * sizeof *map);
    memoryError(map, "recover_highlights()");
    for ( c = 0; c < IMAGE_colors; c++ ) {
        if ( c != kc ) {
            memset(map, 0, high * wide * sizeof *map);
            for ( mrow = 0; mrow < high; mrow++ ) {
                for ( mcol = 0; mcol < wide; mcol++ ) {
                    sum = wgt = count = 0;
                    for ( row = mrow * SCALE; row < (mrow + 1) * SCALE; row++ ) {
                        for ( col = mcol * SCALE; col < (mcol + 1) * SCALE; col++ ) {
                            pixel = GLOBAL_image[row * width + col];
                            if ( pixel[c] / hsat[c] == 1 && pixel[kc] > 24000 ) {
                                sum += pixel[c];
                                wgt += pixel[kc];
                                count++;
                            }
                        }
                    }
                    if ( count == SCALE * SCALE) {
                        map[mrow * wide + mcol] = sum / wgt;
                    }
                }
            }
            for ( spread = 32 / grow; spread--; ) {
                for ( mrow = 0; mrow < high; mrow++ ) {
                    for ( mcol = 0; mcol < wide; mcol++ ) {
                        if ( map[mrow * wide + mcol] ) {
                            continue;
                        }
                        sum = count = 0;
                        for ( d = 0; d < 8; d++ ) {
                            y = mrow + dir[d][0];
                            x = mcol + dir[d][1];
                            if ( y < high && x < wide && map[y * wide + x] > 0 ) {
                                sum += (1 + (d & 1)) * map[y * wide + x];
                                count += 1 + (d & 1);
                            }
                        }
                        if ( count > 3 ) {
                            map[mrow * wide + mcol] = -(sum + grow) / (count + grow);
                        }
                    }
                }
                for ( change = i = 0; i < high * wide; i++ ) {
                    if ( map[i] < 0 ) {
                        map[i] = -map[i];
                        change = 1;
                    }
                }
                if ( !change ) {
                    break;
                }
            }
            for ( i = 0; i < high * wide; i++ ) {
                if ( map[i] == 0 ) {
                    map[i] = 1;
                }
            }
            for ( mrow = 0; mrow < high; mrow++ ) {
                for ( mcol = 0; mcol < wide; mcol++ ) {
                    for ( row = mrow * SCALE; row < (mrow + 1) * SCALE; row++ ) {
                        for ( col = mcol * SCALE; col < (mcol + 1) * SCALE; col++ ) {
                            pixel = GLOBAL_image[row * width + col];
                            if ( pixel[c] / hsat[c] > 1 ) {
                                val = pixel[kc] * map[mrow * wide + mcol];
                                if ( pixel[c] < val ) {
                                    pixel[c] = CLIP(val);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    free(map);
}

#undef SCALE

void
tiff_get(unsigned base, unsigned *tag, unsigned *type, unsigned *len, unsigned *save) {
    *tag = read2bytes();
    *type = read2bytes();
    *len = read4bytes();
    *save = ftell(GLOBAL_IO_ifp) + 4;

    if ( *len * ("11124811248484"[*type < 14 ? *type : 0] - '0') > 4 ) {
        fseek(GLOBAL_IO_ifp, read4bytes() + base, SEEK_SET);
    }
}

void
parse_thumb_note(int base, unsigned toff, unsigned tlen) {
    unsigned entries;
    unsigned tag;
    unsigned type;
    unsigned len;
    unsigned save;

    entries = read2bytes();
    while ( entries-- ) {
        tiff_get(base, &tag, &type, &len, &save);
        if ( tag == toff ) {
            thumb_offset = read4bytes() + base;
        }
        if ( tag == tlen ) {
            thumb_length = read4bytes();
        }
        fseek(GLOBAL_IO_ifp, save, SEEK_SET);
    }
}

int
parse_tiff_ifd(int base);

void
parse_makernote(int base, int uptag) {
    static const unsigned char xlat[2][256] = {
            {0xc1, 0xbf, 0x6d, 0x0d, 0x59, 0xc5, 0x13, 0x9d, 0x83, 0x61, 0x6b, 0x4f, 0xc7, 0x7f, 0x3d, 0x3d,
                    0x53, 0x59, 0xe3, 0xc7, 0xe9, 0x2f, 0x95, 0xa7, 0x95, 0x1f, 0xdf, 0x7f, 0x2b, 0x29, 0xc7, 0x0d,
                    0xdf, 0x07, 0xef, 0x71, 0x89, 0x3d, 0x13, 0x3d, 0x3b, 0x13, 0xfb, 0x0d, 0x89, 0xc1, 0x65, 0x1f,
                    0xb3, 0x0d, 0x6b, 0x29, 0xe3, 0xfb, 0xef, 0xa3, 0x6b, 0x47, 0x7f, 0x95, 0x35, 0xa7, 0x47, 0x4f,
                    0xc7, 0xf1, 0x59, 0x95, 0x35, 0x11, 0x29, 0x61, 0xf1, 0x3d, 0xb3, 0x2b, 0x0d, 0x43, 0x89, 0xc1,
                    0x9d, 0x9d, 0x89, 0x65, 0xf1, 0xe9, 0xdf, 0xbf, 0x3d, 0x7f, 0x53, 0x97, 0xe5, 0xe9, 0x95, 0x17,
                    0x1d, 0x3d, 0x8b, 0xfb, 0xc7, 0xe3, 0x67, 0xa7, 0x07, 0xf1, 0x71, 0xa7, 0x53, 0xb5, 0x29, 0x89,
                    0xe5, 0x2b, 0xa7, 0x17, 0x29, 0xe9, 0x4f, 0xc5, 0x65, 0x6d, 0x6b, 0xef, 0x0d, 0x89, 0x49, 0x2f,
                    0xb3, 0x43, 0x53, 0x65, 0x1d, 0x49, 0xa3, 0x13, 0x89, 0x59, 0xef, 0x6b, 0xef, 0x65, 0x1d, 0x0b,
                    0x59, 0x13, 0xe3, 0x4f, 0x9d, 0xb3, 0x29, 0x43, 0x2b, 0x07, 0x1d, 0x95, 0x59, 0x59, 0x47, 0xfb,
                    0xe5, 0xe9, 0x61, 0x47, 0x2f, 0x35, 0x7f, 0x17, 0x7f, 0xef, 0x7f, 0x95, 0x95, 0x71, 0xd3, 0xa3,
                    0x0b, 0x71, 0xa3, 0xad, 0x0b, 0x3b, 0xb5, 0xfb, 0xa3, 0xbf, 0x4f, 0x83, 0x1d, 0xad, 0xe9, 0x2f,
                    0x71, 0x65, 0xa3, 0xe5, 0x07, 0x35, 0x3d, 0x0d, 0xb5, 0xe9, 0xe5, 0x47, 0x3b, 0x9d, 0xef, 0x35,
                    0xa3, 0xbf, 0xb3, 0xdf, 0x53, 0xd3, 0x97, 0x53, 0x49, 0x71, 0x07, 0x35, 0x61, 0x71, 0x2f, 0x43,
                    0x2f, 0x11, 0xdf, 0x17, 0x97, 0xfb, 0x95, 0x3b, 0x7f, 0x6b, 0xd3, 0x25, 0xbf, 0xad, 0xc7, 0xc5,
                    0xc5, 0xb5, 0x8b, 0xef, 0x2f, 0xd3, 0x07, 0x6b, 0x25, 0x49, 0x95, 0x25, 0x49, 0x6d, 0x71, 0xc7},
            {0xa7, 0xbc, 0xc9, 0xad, 0x91, 0xdf, 0x85, 0xe5, 0xd4, 0x78, 0xd5, 0x17, 0x46, 0x7c, 0x29, 0x4c,
                    0x4d, 0x03, 0xe9, 0x25, 0x68, 0x11, 0x86, 0xb3, 0xbd, 0xf7, 0x6f, 0x61, 0x22, 0xa2, 0x26, 0x34,
                    0x2a, 0xbe, 0x1e, 0x46, 0x14, 0x68, 0x9d, 0x44, 0x18, 0xc2, 0x40, 0xf4, 0x7e, 0x5f, 0x1b, 0xad,
                    0x0b, 0x94, 0xb6, 0x67, 0xb4, 0x0b, 0xe1, 0xea, 0x95, 0x9c, 0x66, 0xdc, 0xe7, 0x5d, 0x6c, 0x05,
                    0xda, 0xd5, 0xdf, 0x7a, 0xef, 0xf6, 0xdb, 0x1f, 0x82, 0x4c, 0xc0, 0x68, 0x47, 0xa1, 0xbd, 0xee,
                    0x39, 0x50, 0x56, 0x4a, 0xdd, 0xdf, 0xa5, 0xf8, 0xc6, 0xda, 0xca, 0x90, 0xca, 0x01, 0x42, 0x9d,
                    0x8b, 0x0c, 0x73, 0x43, 0x75, 0x05, 0x94, 0xde, 0x24, 0xb3, 0x80, 0x34, 0xe5, 0x2c, 0xdc, 0x9b,
                    0x3f, 0xca, 0x33, 0x45, 0xd0, 0xdb, 0x5f, 0xf5, 0x52, 0xc3, 0x21, 0xda, 0xe2, 0x22, 0x72, 0x6b,
                    0x3e, 0xd0, 0x5b, 0xa8, 0x87, 0x8c, 0x06, 0x5d, 0x0f, 0xdd, 0x09, 0x19, 0x93, 0xd0, 0xb9, 0xfc,
                    0x8b, 0x0f, 0x84, 0x60, 0x33, 0x1c, 0x9b, 0x45, 0xf1, 0xf0, 0xa3, 0x94, 0x3a, 0x12, 0x77, 0x33,
                    0x4d, 0x44, 0x78, 0x28, 0x3c, 0x9e, 0xfd, 0x65, 0x57, 0x16, 0x94, 0x6b, 0xfb, 0x59, 0xd0, 0xc8,
                    0x22, 0x36, 0xdb, 0xd2, 0x63, 0x98, 0x43, 0xa1, 0x04, 0x87, 0x86, 0xf7, 0xa6, 0x26, 0xbb, 0xd6,
                    0x59, 0x4d, 0xbf, 0x6a, 0x2e, 0xaa, 0x2b, 0xef, 0xe6, 0x78, 0xb6, 0x4e, 0xe0, 0x2f, 0xdc, 0x7c,
                    0xbe, 0x57, 0x19, 0x32, 0x7e, 0x2a, 0xd0, 0xb8, 0xba, 0x29, 0x00, 0x3c, 0x52, 0x7d, 0xa8, 0x49,
                    0x3b, 0x2d, 0xeb, 0x25, 0x49, 0xfa, 0xa3, 0xaa, 0x39, 0xa7, 0xc5, 0xa7, 0x50, 0x11, 0x36, 0xfb,
                    0xc6, 0x67, 0x4a, 0xf5, 0xa5, 0x12, 0x65, 0x7e, 0xb0, 0xdf, 0xaf, 0x4e, 0xb3, 0x61, 0x7f, 0x2f}};
    unsigned offset = 0;
    unsigned entries;
    unsigned tag;
    unsigned type;
    unsigned len;
    unsigned save;
    unsigned c;
    unsigned ver97 = 0;
    unsigned serial = 0;
    unsigned i;
    unsigned wbi = 0;
    unsigned wb[4] = {0, 0, 0, 0};
    unsigned char buf97[324];
    unsigned char ci;
    unsigned char cj;
    unsigned char ck;
    short morder;
    short sorder = GLOBAL_endianOrder;
    char buf[10];

    // The MakerNote might have its own TIFF header (possibly with its own byte-order!), or it might just be a table.
    if ( !strcmp(GLOBAL_make, "Nokia") ) {
        return;
    }
    fread(buf, 1, 10, GLOBAL_IO_ifp);
    if ( !strncmp(buf, "KDK", 3) || // these aren't TIFF tables
         !strncmp(buf, "VER", 3) ||
         !strncmp(buf, "IIII", 4) ||
         !strncmp(buf, "MMMM", 4) ) {
        return;
    }

    if ( !strncmp(buf, "KC", 2) ||  // Konica KD-400Z, KD-510Z
         !strncmp(buf, "MLY", 3)) { // Minolta DiMAGE G series
        GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
        while ((i = ftell(GLOBAL_IO_ifp)) < GLOBAL_IO_profileOffset && i < 16384 ) {
            wb[0] = wb[2];
            wb[2] = wb[1];
            wb[1] = wb[3];
            wb[3] = read2bytes();
            if ( wb[1] == 256 && wb[3] == 256 &&
                 wb[0] > 256 && wb[0] < 640 && wb[2] > 256 && wb[2] < 640 ) {
                for ( c = 0; c < 4; c++ ) {
                    GLOBAL_cam_mul[c] = wb[c];
                }
            }
        }
        goto quit;
    }

    if ( !strcmp(buf, "Nikon") ) {
        base = ftell(GLOBAL_IO_ifp);
        GLOBAL_endianOrder = read2bytes();
        if ( read2bytes() != 42 ) {
            goto quit;
        }
        offset = read4bytes();
        fseek(GLOBAL_IO_ifp, offset - 8, SEEK_CUR);
    } else {
        if ( !strcmp(buf, "OLYMPUS") || !strcmp(buf, "PENTAX ") ) {
            base = ftell(GLOBAL_IO_ifp) - 10;
            fseek(GLOBAL_IO_ifp, -2, SEEK_CUR);
            GLOBAL_endianOrder = read2bytes();
            if ( buf[0] == 'O' ) read2bytes();
        } else {
            if ( !strncmp(buf, "SONY", 4) || !strcmp(buf, "Panasonic") ) {
                goto nf;
            } else {
                if ( !strncmp(buf, "FUJIFILM", 8) ) {
                    base = ftell(GLOBAL_IO_ifp) - 10;
                    nf:
                    GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
                    fseek(GLOBAL_IO_ifp, 2, SEEK_CUR);
                } else {
                    if ( !strcmp(buf, "OLYMP") ||
                         !strcmp(buf, "LEICA") ||
                         !strcmp(buf, "Ricoh") ||
                         !strcmp(buf, "EPSON") ) {
                        fseek(GLOBAL_IO_ifp, -2, SEEK_CUR);
                    } else {
                        if ( !strcmp(buf, "AOC") ||
                             !strcmp(buf, "QVC") ) {
                            fseek(GLOBAL_IO_ifp, -4, SEEK_CUR);
                        } else {
                            fseek(GLOBAL_IO_ifp, -10, SEEK_CUR);
                            if ( !strncmp(GLOBAL_make, "SAMSUNG", 7) ) {
                                base = ftell(GLOBAL_IO_ifp);
                            }
                        }
                    }
                }
            }
        }
    }

    entries = read2bytes();
    if ( entries > 1000 ) {
        return;
    }
    morder = GLOBAL_endianOrder;
    while ( entries-- ) {
        GLOBAL_endianOrder = morder;
        tiff_get(base, &tag, &type, &len, &save);
        tag |= uptag << 16;

        if ( tag == 2 && strstr(GLOBAL_make, "NIKON") && !iso_speed ) {
            iso_speed = (read2bytes(), read2bytes());
        }

        if ( tag == 4 && len > 26 && len < 35 ) {
            if ((i = (read4bytes(), read2bytes())) != 0x7fff && !iso_speed ) {
                iso_speed = 50 * pow(2, i / 32.0 - 4);
            }
            if ((i = (read2bytes(), read2bytes())) != 0x7fff && !aperture ) {
                aperture = pow(2, i / 64.0);
            }
            if ((i = read2bytes()) != 0xffff && !CAMERA_IMAGE_information.shutterSpeed ) {
                CAMERA_IMAGE_information.shutterSpeed = pow(2, (short) i / -32.0);
            }
            wbi = (read2bytes(), read2bytes());
            shot_order = (read2bytes(), read2bytes());
        }

        if ( (tag == 4 || tag == 0x114) && !strncmp(GLOBAL_make, "KONICA", 6) ) {
            fseek(GLOBAL_IO_ifp, tag == 4 ? 140 : 160, SEEK_CUR);
            switch ( read2bytes() ) {
                case 72:
                    GLOBAL_flipsMask = 0;
                    break;
                case 76:
                    GLOBAL_flipsMask = 6;
                    break;
                case 82:
                    GLOBAL_flipsMask = 5;
                    break;
            }
        }

        if ( tag == 7 && type == 2 && len > 20 ) {
            fgets(model2, 64, GLOBAL_IO_ifp);
        }

        if ( tag == 8 && type == 4 ) {
            shot_order = read4bytes();
        }

        if ( tag == 9 && !strcmp(GLOBAL_make, "Canon") ) {
            fread(artist, 64, 1, GLOBAL_IO_ifp);
        }

        if ( tag == 0xc && len == 4 ) {
            for ( c = 0; c < 3; c++ ) {
                GLOBAL_cam_mul[(c << 1 | c >> 1) & 3] = readDouble(type);
            }
        }

        if ( tag == 0xd && type == 7 && read2bytes() == 0xaaaa ) {
            for ( c = i = 2; (unsigned short) c != 0xbbbb && i < len; i++ ) {
                c = c << 8 | fgetc(GLOBAL_IO_ifp);
            }
            while ( (i += 4) < len - 5 ) {
                if ( read4bytes() == 257 && (i = len) && (c = (read4bytes(), fgetc(GLOBAL_IO_ifp))) < 3 ) {
                    GLOBAL_flipsMask = "065"[c] - '0';
                }
            }
        }

        if ( tag == 0x10 && type == 4 ) {
            unique_id = read4bytes();
        }

        if ( tag == 0x11 && is_raw && !strncmp(GLOBAL_make, "NIKON", 5)) {
            fseek(GLOBAL_IO_ifp, read4bytes() + base, SEEK_SET);
            parse_tiff_ifd(base);
        }

        if ( tag == 0x14 && type == 7 ) {
            if ( len == 2560 ) {
                fseek(GLOBAL_IO_ifp, 1248, SEEK_CUR);
                goto get2_256;
            }
            fread(buf, 1, 10, GLOBAL_IO_ifp);
            if ( !strncmp(buf, "NRW ", 4) ) {
                fseek(GLOBAL_IO_ifp, strcmp(buf + 4, "0100") ? 46 : 1546, SEEK_CUR);
                GLOBAL_cam_mul[0] = read4bytes() << 2;
                GLOBAL_cam_mul[1] = read4bytes() + read4bytes();
                GLOBAL_cam_mul[2] = read4bytes() << 2;
            }
        }

        if ( tag == 0x15 && type == 2 && is_raw ) {
            fread(GLOBAL_model, 64, 1, GLOBAL_IO_ifp);
        }

        if ( strstr(GLOBAL_make, "PENTAX") ) {
            if ( tag == 0x1b ) {
                tag = 0x1018;
            }
            if ( tag == 0x1c ) {
                tag = 0x1017;
            }
        }

        if ( tag == 0x1d ) {
            while ((c = fgetc(GLOBAL_IO_ifp)) && c != EOF) {
                serial = serial * 10 + (isdigit(c) ? c - '0' : c % 10);
            }
        }

        if ( tag == 0x29 && type == 1 ) {
            c = wbi < 18 ? "012347800000005896"[wbi] - '0' : 0;
            fseek(GLOBAL_IO_ifp, 8 + c * 32, SEEK_CUR);
            for ( c = 0; c < 4; c++ ) {
                GLOBAL_cam_mul[c ^ (c >> 1) ^ 1] = read4bytes();
            }
        }

        if ( tag == 0x3d && type == 3 && len == 4 ) {
            for ( c = 0; c < 4; c++ ) {
                cblack[c ^ c >> 1] = read2bytes() >> (14 - THE_image.bitsPerSample);
            }
        }

        if ( tag == 0x81 && type == 4 ) {
            GLOBAL_IO_profileOffset = read4bytes();
            fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset + 41, SEEK_SET);
            THE_image.height = read2bytes() * 2;
            THE_image.width = read2bytes();
            IMAGE_filters = 0x61616161;
        }

        if ( (tag == 0x81 && type == 7) ||
             (tag == 0x100 && type == 7) ||
             (tag == 0x280 && type == 1) ) {
            thumb_offset = ftell(GLOBAL_IO_ifp);
            thumb_length = len;
        }

        if ( tag == 0x88 && type == 4 && (thumb_offset = read4bytes()) ) {
            thumb_offset += base;
        }

        if ( tag == 0x89 && type == 4 ) {
            thumb_length = read4bytes();
        }

        if ( tag == 0x8c || tag == 0x96 ) {
            GLOBAL_meta_offset = ftell(GLOBAL_IO_ifp);
        }

        if ( tag == 0x97 ) {
            for ( i = 0; i < 4; i++ ) {
                ver97 = ver97 * 10 + fgetc(GLOBAL_IO_ifp) - '0';
            }
            switch ( ver97 ) {
                case 100:
                    fseek(GLOBAL_IO_ifp, 68, SEEK_CUR);
                    for ( c = 0; c < 4; c++ ) {
                        GLOBAL_cam_mul[(c >> 1) | ((c & 1) << 1)] = read2bytes();
                    }
                    break;
                case 102:
                    fseek(GLOBAL_IO_ifp, 6, SEEK_CUR);
                    for ( c = 0; c < 4; c++ ) {
                        GLOBAL_cam_mul[c ^ (c >> 1)] = read2bytes();
                    }
                    break;
                case 103:
                    fseek(GLOBAL_IO_ifp, 16, SEEK_CUR);
                    for ( c = 0; c < 4; c++ ) {
                        GLOBAL_cam_mul[c] = read2bytes();
                    }
                    break;
                default:
                    break;
            }
            if ( ver97 >= 200 ) {
                if ( ver97 != 205 ) {
                    fseek(GLOBAL_IO_ifp, 280, SEEK_CUR);
                }
                fread(buf97, 324, 1, GLOBAL_IO_ifp);
            }
        }

        if ( tag == 0xa1 && type == 7 ) {
            GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
            fseek(GLOBAL_IO_ifp, 140, SEEK_CUR);
            for ( c = 0; c < 3; c++ ) {
                GLOBAL_cam_mul[c] = read4bytes();
            }
        }

        if ( tag == 0xa4 && type == 3 ) {
            fseek(GLOBAL_IO_ifp, wbi * 48, SEEK_CUR);
            for ( c = 0; c < 3; c++ ) {
                GLOBAL_cam_mul[c] = read2bytes();
            }
        }

        if ( tag == 0xa7 && (unsigned) (ver97 - 200) < 17 ) {
            ci = xlat[0][serial & 0xff];
            cj = xlat[1][fgetc(GLOBAL_IO_ifp) ^ fgetc(GLOBAL_IO_ifp) ^ fgetc(GLOBAL_IO_ifp) ^ fgetc(GLOBAL_IO_ifp)];
            ck = 0x60;
            for ( i = 0; i < 324; i++ ) {
                buf97[i] ^= (cj += ci * ck++);
            }
            i = "66666>666;6A;:;55"[ver97 - 200] - '0';
            for ( c = 0; c < 4; c++ ) {
                GLOBAL_cam_mul[c ^ (c >> 1) ^ (i & 1)] = unsignedShortEndianSwap(buf97 + (i & -2) + c * 2);
            }
        }

        if ( tag == 0x200 && len == 3 ) {
            shot_order = (read4bytes(), read4bytes());
        }

        if ( tag == 0x200 && len == 4 ) {
            for ( c = 0; c < 4; c++ ) {
                cblack[c ^ c >> 1] = read2bytes();
            }
        }

        if ( tag == 0x201 && len == 4 ) {
            for ( c = 0; c < 4; c++ ) {
                GLOBAL_cam_mul[c ^ (c >> 1)] = read2bytes();
            }
        }

        if ( tag == 0x220 && type == 7 ) {
            GLOBAL_meta_offset = ftell(GLOBAL_IO_ifp);
        }

        if ( tag == 0x401 && type == 4 && len == 4 ) {
            for ( c = 0; c < 4; c++ ) {
                cblack[c ^ c >> 1] = read4bytes();
            }
        }

        if ( tag == 0xe01 ) {
            // Nikon Capture Note
            GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
            fseek(GLOBAL_IO_ifp, 22, SEEK_CUR);
            for ( offset = 22; offset + 22 < len; offset += 22 + i ) {
                tag = read4bytes();
                fseek(GLOBAL_IO_ifp, 14, SEEK_CUR);
                i = read4bytes() - 4;
                if ( tag == 0x76a43207 ) {
                    GLOBAL_flipsMask = read2bytes();
                } else {
                    fseek(GLOBAL_IO_ifp, i, SEEK_CUR);
                }
            }
        }

        if ( tag == 0xe80 && len == 256 && type == 7 ) {
            fseek(GLOBAL_IO_ifp, 48, SEEK_CUR);
            GLOBAL_cam_mul[0] = read2bytes() * 508 * 1.078 / 0x10000;
            GLOBAL_cam_mul[2] = read2bytes() * 382 * 1.173 / 0x10000;
        }

        if ( tag == 0xf00 && type == 7 ) {
            if ( len == 614 ) {
                fseek(GLOBAL_IO_ifp, 176, SEEK_CUR);
            } else {
                if ( len == 734 || len == 1502 ) {
                    fseek(GLOBAL_IO_ifp, 148, SEEK_CUR);
                } else {
                    goto next;
                }
            }
            goto get2_256;
        }

        if ( (tag == 0x1011 && len == 9) || tag == 0x20400200 ) {
            for ( i = 0; i < 3; i++ ) {
                for ( c = 0; c < 3; c++ ) {
                    cmatrix[i][c] = ((short) read2bytes()) / 256.0;
                }
            }
        }

        if ( (tag == 0x1012 || tag == 0x20400600) && len == 4 ) {
            for ( c = 0; c < 4; c++ ) {
                cblack[c ^ c >> 1] = read2bytes();
            }
        }

        if ( tag == 0x1017 || tag == 0x20400100 ) {
            GLOBAL_cam_mul[0] = read2bytes() / 256.0;
        }

        if ( tag == 0x1018 || tag == 0x20400100 ) {
            GLOBAL_cam_mul[2] = read2bytes() / 256.0;
        }

        if ( tag == 0x2011 && len == 2 ) {
            get2_256:
            GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
            GLOBAL_cam_mul[0] = read2bytes() / 256.0;
            GLOBAL_cam_mul[2] = read2bytes() / 256.0;
        }

        if ( (tag | 0x70) == 0x2070 && (type == 4 || type == 13) ) {
            fseek(GLOBAL_IO_ifp, read4bytes() + base, SEEK_SET);
        }

        if ( tag == 0x2020 && !strncmp(buf, "OLYMP", 5) ) {
            parse_thumb_note(base, 257, 258);
        }

        if ( tag == 0x2040 ) {
            parse_makernote(base, 0x2040);
        }

        if ( tag == 0xb028 ) {
            fseek(GLOBAL_IO_ifp, read4bytes() + base, SEEK_SET);
            parse_thumb_note(base, 136, 137);
        }

        if ( tag == 0x4001 && len > 500 ) {
            i = len == 582 ? 50 : len == 653 ? 68 : len == 5120 ? 142 : 126;
            fseek(GLOBAL_IO_ifp, i, SEEK_CUR);
            for ( c = 0; c < 4; c++ ) {
                GLOBAL_cam_mul[c ^ (c >> 1)] = read2bytes();
            };
            for ( i += 18; i <= len; i += 10 ) {
                read2bytes();
                for ( c = 0; c < 4; c++ ) {
                    sraw_mul[c ^ (c >> 1)] = read2bytes();
                }
                if ( sraw_mul[1] == 1170 ) {
                    break;
                }
            }
        }

        if ( tag == 0x4021 && read4bytes() && read4bytes() ) {
            for ( c = 0; c < 4; c++ ) {
                GLOBAL_cam_mul[c] = 1024;
            }
        }

        if ( tag == 0xa021 ) {
            for ( c = 0; c < 4; c++ ) {
                GLOBAL_cam_mul[c ^ (c >> 1)] = read4bytes();
            }
        }

        if ( tag == 0xa028 ) {
            for ( c = 0; c < 4; c++ ) {
                GLOBAL_cam_mul[c ^ (c >> 1)] -= read4bytes();
            }
        }

        if ( tag == 0xb001 ) {
            unique_id = read2bytes();
        }

        next:
        fseek(GLOBAL_IO_ifp, save, SEEK_SET);
    }
    quit:
    GLOBAL_endianOrder = sorder;
}

/*
Since the TIFF DateTime string has no timezone information,
assume that the camera's clock was set to Universal Time.
*/
void
get_timestamp(int reversed) {
    struct tm t;
    char str[20];
    int i;

    str[19] = 0;
    if ( reversed ) {
        for ( i = 19; i--; ) {
            str[i] = fgetc(GLOBAL_IO_ifp);
        }
    } else {
        fread(str, 19, 1, GLOBAL_IO_ifp);
    }
    memset(&t, 0, sizeof t);
    if ( sscanf(str, "%d:%d:%d %d:%d:%d", &t.tm_year, &t.tm_mon,
                &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) != 6 ) {
        return;
    }
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    t.tm_isdst = -1;
    if ( mktime(&t) > 0 ) {
        timestamp = mktime(&t);
    }
}

void
parse_exif(int base) {
    unsigned kodak;
    unsigned entries;
    unsigned tag;
    unsigned type;
    unsigned len;
    unsigned save;
    unsigned c;
    double expo;

    kodak = !strncmp(GLOBAL_make, "EASTMAN", 7) && numberOfRawImages < 3;
    entries = read2bytes();
    while ( entries-- ) {
        tiff_get(base, &tag, &type, &len, &save);
        switch ( tag ) {
            case EXIFTAG_EXPOSURETIME:
                ifdArray[numberOfRawImages - 1].shutter =
                CAMERA_IMAGE_information.shutterSpeed = readDouble(type);
                break;
            case 33437:
                aperture = readDouble(type);
                break;
            case 34855:
                iso_speed = read2bytes();
                break;
            case 36867:
            case 36868:
                get_timestamp(0);
                break;
            case 37377:
                if ((expo = -readDouble(type)) < 128 ) {
                    ifdArray[numberOfRawImages - 1].shutter =
                    CAMERA_IMAGE_information.shutterSpeed = pow(2, expo);
                }
                break;
            case 37378:
                aperture = pow(2, readDouble(type) / 2);
                break;
            case 37386:
                focal_len = readDouble(type);
                break;
            case 37500:
                parse_makernote(base, 0);
                break;
            case 40962:
                if ( kodak ) {
                    THE_image.width = read4bytes();
                }
                break;
            case 40963:
                if ( kodak ) {
                    THE_image.height = read4bytes();
                }
                break;
            case 41730:
                if ( read4bytes() == 0x20002 ) {
                    for ( exif_cfa = c = 0; c < 8; c += 2 ) {
                        exif_cfa |= fgetc(GLOBAL_IO_ifp) * 0x01010101 << c;
                    }
                }
                break;
            default:
                break;
        }
        fseek(GLOBAL_IO_ifp, save, SEEK_SET);
    }
}

void
parse_gps(int base) {
    unsigned entries;
    unsigned tag;
    unsigned type;
    unsigned len;
    unsigned save;
    unsigned c;

    entries = read2bytes();
    while ( entries-- ) {
        tiff_get(base, &tag, &type, &len, &save);
        switch ( tag ) {
            case 1:
            case 3:
            case 5:
                gpsdata[29 + tag / 2] = getc(GLOBAL_IO_ifp);
                break;
            case 2:
            case 4:
            case 7:
                for ( c = 0; c < 6; c++ ) {
                    gpsdata[tag / 3 * 6 + c] = read4bytes();
                }
                break;
            case 6:
                for ( c = 0; c < 2; c++ ) {
                    gpsdata[18 + c] = read4bytes();
                }
                break;
            case 18:
            case 29:
                fgets((char *) (gpsdata + 14 + tag / 3), MIN(len, 12), GLOBAL_IO_ifp);
                break;
            default:
                break;
        }
        fseek(GLOBAL_IO_ifp, save, SEEK_SET);
    }
}

void
romm_coeff(float romm_cam[3][3]) {
    static const float rgb_romm[3][3] = // ROMM == Kodak ProPhoto
            {{2.034193,  -0.727420, -0.306766},
             {-0.228811, 1.231729,  -0.002922},
             {-0.008565, -0.153273, 1.161839}};
    int i;
    int j;
    int k;

    for ( i = 0; i < 3; i++ ) {
        for ( j = 0; j < 3; j++ ) {
            for ( cmatrix[i][j] = k = 0; k < 3; k++ ) {
                cmatrix[i][j] += rgb_romm[i][k] * romm_cam[k][j];
            }
        }
    }
}

void
parse_mos(int offset) {
    char data[40];
    int skip;
    int from;
    int i;
    int c;
    int neut[4];
    int planes = 0;
    int frot = 0;
    static const char *mod[] =
            {"", "DCB2", "Volare", "Cantare", "CMost", "Valeo 6", "Valeo 11", "Valeo 22",
             "Valeo 11p", "Valeo 17", "", "Aptus 17", "Aptus 22", "Aptus 75", "Aptus 65",
             "Aptus 54S", "Aptus 65S", "Aptus 75S", "AFi 5", "AFi 6", "AFi 7",
             "AFi-II 7", "Aptus-II 7", "", "Aptus-II 6", "", "", "Aptus-II 10", "Aptus-II 5",
             "", "", "", "", "Aptus-II 10R", "Aptus-II 8", "", "Aptus-II 12", "", "AFi-II 12"};
    float romm_cam[3][3];

    fseek(GLOBAL_IO_ifp, offset, SEEK_SET);
    while ( 1 ) {
        if ( read4bytes() != 0x504b5453 ) {
            break;
        }
        read4bytes();
        fread(data, 1, 40, GLOBAL_IO_ifp);
        skip = read4bytes();
        from = ftell(GLOBAL_IO_ifp);

        if ( !strcmp(data, "JPEG_preview_data") ) {
            thumb_offset = from;
            thumb_length = skip;
        }

        if ( !strcmp(data, "icc_camera_profile") ) {
            profile_offset = from;
            profile_length = skip;
        }

        if ( !strcmp(data, "ShootObj_back_type") ) {
            fscanf(GLOBAL_IO_ifp, "%d", &i);
            if ( (unsigned) i < sizeof mod / sizeof(*mod) ) {
                strcpy(GLOBAL_model, mod[i]);
            }
        }

        if ( !strcmp(data, "icc_camera_to_tone_matrix") ) {
            for ( i = 0; i < 9; i++ ) {
                ((float *) romm_cam)[i] = intToFloat(read4bytes());
            }
            romm_coeff(romm_cam);
        }

        if ( !strcmp(data, "CaptProf_color_matrix") ) {
            for ( i = 0; i < 9; i++ ) {
                fscanf(GLOBAL_IO_ifp, "%f", (float *) romm_cam + i);
            }
            romm_coeff(romm_cam);
        }

        if ( !strcmp(data, "CaptProf_number_of_planes") ) {
            fscanf(GLOBAL_IO_ifp, "%d", &planes);
        }

        if ( !strcmp(data, "CaptProf_raw_data_rotation") ) {
            fscanf(GLOBAL_IO_ifp, "%d", &GLOBAL_flipsMask);
        }

        if ( !strcmp(data, "CaptProf_mosaic_pattern") ) {
            for ( c = 0; c < 4; c++ ) {
                fscanf(GLOBAL_IO_ifp, "%d", &i);
                if ( i == 1 ) frot = c ^ (c >> 1);
            }
        }

        if ( !strcmp(data, "ImgProf_rotation_angle") ) {
            fscanf(GLOBAL_IO_ifp, "%d", &i);
            GLOBAL_flipsMask = i - GLOBAL_flipsMask;
        }

        if ( !strcmp(data, "NeutObj_neutrals") && !GLOBAL_cam_mul[0] ) {
            for ( c = 0; c < 4; c++ ) {
                fscanf(GLOBAL_IO_ifp, "%d", neut + c);
            }
            for ( c = 0; c < 3; c++ ) {
                GLOBAL_cam_mul[c] = (float) neut[0] / neut[c + 1];
            }
        }

        if ( !strcmp(data, "Rows_data") ) {
            GLOBAL_loadFlags = read4bytes();
        }
        parse_mos(from);
        fseek(GLOBAL_IO_ifp, skip + from, SEEK_SET);
    }

    if ( planes ) {
        IMAGE_filters = (planes == 1) * 0x01010101 *
                        (unsigned char)"\x94\x61\x16\x49"[(GLOBAL_flipsMask / 90 + frot) & 3];
    }
}

void
linear_table(unsigned len) {
    int i;

    if ( len > 0x1000 ) {
        len = 0x1000;
    }
    readShorts(GAMMA_curveFunctionLookupTable, len);
    for ( i = len; i < 0x1000; i++ ) {
        GAMMA_curveFunctionLookupTable[i] = GAMMA_curveFunctionLookupTable[i - 1];
    }
    ADOBE_maximum = GAMMA_curveFunctionLookupTable[0xfff];
}

void
parse_kodak_ifd(int base) {
    unsigned entries;
    unsigned tag;
    unsigned type;
    unsigned len;
    unsigned save;
    int i;
    int c;
    int wbi = -2;
    int wbtemp = 6500;
    float mul[3] = {1, 1, 1};
    float num;
    static const int wbtag[] = {64037, 64040, 64039, 64041, -1, -1, 64042};

    entries = read2bytes();
    if ( entries > 1024 ) {
        return;
    }

    while ( entries-- ) {
        tiff_get(base, &tag, &type, &len, &save);

        if ( tag == 1020 ) {
            wbi = readInt(type);
        }

        if ( tag == 1021 && len == 72 ) {
            // WB set in software
            fseek(GLOBAL_IO_ifp, 40, SEEK_CUR);
            for ( c = 0; c < 4; c++ ) {
                GLOBAL_cam_mul[c] = 2048.0 / read2bytes();
            }
            wbi = -2;
        }

        if ( tag == 2118 ) {
            wbtemp = readInt(type);
        }

        if ( tag == 2120 + wbi && wbi >= 0 ) {
            for ( c = 0; c < 3; c++ ) {
                GLOBAL_cam_mul[c] = 2048.0 / readDouble(type);
            }
        }

        if ( tag == 2130 + wbi ) {
            for ( c = 0; c < 3; c++ ) {
                mul[c] = readDouble(type);
            }
        }

        if ( tag == 2140 + wbi && wbi >= 0 ) {
            for ( c = 0; c < 3; c++ ) {
                for ( num = i = 0; i < 4; i++ ) {
                    num += readDouble(type) * pow(wbtemp / 100.0, i);
                }
                GLOBAL_cam_mul[c] = 2048 / (num * mul[c]);
            }
        }

        if ( tag == 2317 ) {
            linear_table(len);
        }

        if ( tag == 6020 ) {
            iso_speed = readInt(type);
        }

        if ( tag == 64013 ) {
            wbi = fgetc(GLOBAL_IO_ifp);
        }

        if ( (unsigned) wbi < 7 && tag == wbtag[wbi] ) {
            for ( c = 0; c < 3; c++ ) {
                GLOBAL_cam_mul[c] = read4bytes();
            }
        }

        if ( tag == 64019 ) {
            width = readInt(type);
        }

        if ( tag == 64020 ) {
            height = (readInt(type) + 1) & -2;
        }

        fseek(GLOBAL_IO_ifp, save, SEEK_SET);
    }
}

void parse_minolta(int base);

int parse_tiff(int base);

int parse_tiff_ifd(int base) {
    unsigned entries;
    unsigned tag;
    unsigned type;
    unsigned len;
    unsigned plen = 16;
    unsigned save;
    int ifd;
    int use_cm = 0;
    int cfa;
    int i;
    int j;
    int c;
    int ima_len = 0;
    char software[64];
    char *cbuf;
    char *cp;
    unsigned char cfa_pat[16];
    unsigned char cfa_pc[] = {0, 1, 2, 3};
    unsigned char tab[256];
    double cc[4][4];
    double cm[4][3];
    double cam_xyz[4][3];
    double num;
    double ab[] = {1, 1, 1, 1};
    double asn[] = {0, 0, 0, 0};
    double xyz[] = {1, 1, 1};
    unsigned sony_curve[] = {0, 0, 0, 0, 0, 4095};
    unsigned *buf;
    unsigned sony_offset = 0;
    unsigned sony_length = 0;
    unsigned sony_key = 0;
    struct jhead jh;
    FILE *sfp;

    if ( numberOfRawImages >= sizeof ifdArray / sizeof ifdArray[0] ) {
        return 1;
    }
    ifd = numberOfRawImages++;
    for ( j = 0; j < 4; j++ ) {
        for ( i = 0; i < 4; i++ ) {
            cc[j][i] = i == j;
        }
    }
    entries = read2bytes();
    if ( entries > 512 ) {
        return 1;
    }
    while ( entries-- ) {
        tiff_get(base, &tag, &type, &len, &save);
        switch ( tag ) {
            case 5:
                width = read2bytes();
                break;
            case 6:
                height = read2bytes();
                break;
            case 7:
                width += read2bytes();
                break;
            case 9:
                if ( (i = read2bytes()) ) {
                    IMAGE_filters = i;
                }
                break;
            case 17:
            case 18:
                if ( type == 3 && len == 1 ) {
                    GLOBAL_cam_mul[(tag - 17) * 2] = read2bytes() / 256.0;
                }
                break;
            case 23:
                if ( type == 3 ) {
                    iso_speed = read2bytes();
                }
                break;
            case 28:
            case 29:
            case 30:
                cblack[tag - 28] = read2bytes();
                cblack[3] = cblack[1];
                break;
            case 36:
            case 37:
            case 38:
                GLOBAL_cam_mul[tag - 36] = read2bytes();
                break;
            case 39:
                if ( len < 50 || GLOBAL_cam_mul[0] ) {
                    break;
                }
                fseek(GLOBAL_IO_ifp, 12, SEEK_CUR);
                for ( c = 0; c < 3; c++ ) {
                    GLOBAL_cam_mul[c] = read2bytes();
                }
                break;
            case 46:
                if ( type != 7 || fgetc(GLOBAL_IO_ifp) != 0xff || fgetc(GLOBAL_IO_ifp) != 0xd8 ) {
                    break;
                }
                thumb_offset = ftell(GLOBAL_IO_ifp) - 2;
                thumb_length = len;
                break;
            case 61440:
                // Fuji HS10 table
                fseek(GLOBAL_IO_ifp, read4bytes() + base, SEEK_SET);
                parse_tiff_ifd(base);
                break;
            case 2:
            case 256:
            case 61441:
                // ImageWidth
                ifdArray[ifd].width = readInt(type);
                break;
            case 3:
            case 257:
            case 61442:
                // ImageHeight
                ifdArray[ifd].height = readInt(type);
                break;
            case TIFFTAG_BITSPERSAMPLE:
                // BitsPerSample
            case 61443:
                ifdArray[ifd].samples = len & 7;
                if ( (ifdArray[ifd].bps = readInt(type) ) > 32 ) {
                    ifdArray[ifd].bps = 8;
                }
                if ( THE_image.bitsPerSample < ifdArray[ifd].bps ) {
                    THE_image.bitsPerSample = ifdArray[ifd].bps;
                }
                break;
            case 61446:
                THE_image.height = 0;
                GLOBAL_loadFlags = read4bytes() ? 24 : 80;
                break;
            case 259:
                // Compression
                ifdArray[ifd].comp = readInt(type);
                break;
            case 262:
                // PhotometricInterpretation
                ifdArray[ifd].phint = read2bytes();
                break;
            case 270:
                // ImageDescription
                fread(desc, 512, 1, GLOBAL_IO_ifp);
                break;
            case 271:
                // Make
                fgets(GLOBAL_make, 64, GLOBAL_IO_ifp);
                break;
            case 272:
                // Model
                fgets(GLOBAL_model, 64, GLOBAL_IO_ifp);
                break;
            case 280:
                // Panasonic RW2 offset
                if ( type != 4 ) {
                    break;
                }
                TIFF_CALLBACK_loadRawData = & panasonic_load_raw;
                GLOBAL_loadFlags = 0x2008;
            case 273:
                // StripOffset
            case 513:
                // JpegIFOffset
            case 61447:
                ifdArray[ifd].offset = read4bytes() + base;
                if ( !ifdArray[ifd].bps && ifdArray[ifd].offset > 0 ) {
                    fseek(GLOBAL_IO_ifp, ifdArray[ifd].offset, SEEK_SET);
                    if ( ljpeg_start(&jh, 1) ) {
                        ifdArray[ifd].comp = 6;
                        ifdArray[ifd].width = jh.wide;
                        ifdArray[ifd].height = jh.high;
                        ifdArray[ifd].bps = jh.bits;
                        ifdArray[ifd].samples = jh.clrs;
                        if ( !(jh.sraw || (jh.clrs & 1)) ) {
                            ifdArray[ifd].width *= jh.clrs;
                        }
                        if ((ifdArray[ifd].width > 4 * ifdArray[ifd].height) & ~jh.clrs ) {
                            ifdArray[ifd].width /= 2;
                            ifdArray[ifd].height *= 2;
                        }
                        i = GLOBAL_endianOrder;
                        parse_tiff(ifdArray[ifd].offset + 12);
                        GLOBAL_endianOrder = i;
                    }
                }
                break;
            case 274:
                // Orientation
                ifdArray[ifd].flip = "50132467"[read2bytes() & 7] - '0';
                break;
            case 277:
                // SamplesPerPixel
                ifdArray[ifd].samples = readInt(type) & 7;
                break;
            case 279:
                //StripByteCounts
            case 514:
            case 61448:
                ifdArray[ifd].bytes = read4bytes();
                break;
            case 61454:
                for ( c = 0; c < 3; c++ ) {
                    GLOBAL_cam_mul[(4 - c) % 3] = readInt(type);
                }
                break;
            case 305:
            case 11:
                // Software
                fgets(software, 64, GLOBAL_IO_ifp);
                if ( !strncmp(software, "Adobe", 5) ||
                     !strncmp(software, "dcraw", 5) ||
                     !strncmp(software, "UFRaw", 5) ||
                     !strncmp(software, "Bibble", 6) ||
                     !strncmp(software, "Nikon Scan", 10) ||
                     !strcmp(software, "Digital Photo Professional"))
                    is_raw = 0;
                break;
            case 306:
                // DateTime
                get_timestamp(0);
                break;
            case 315:
                // Artist
                fread(artist, 64, 1, GLOBAL_IO_ifp);
                break;
            case 322:
                // TileWidth
                ifdArray[ifd].tile_width = readInt(type);
                break;
            case 323:
                // TileLength
                ifdArray[ifd].tile_length = readInt(type);
                break;
            case 324:
                // TileOffsets
                ifdArray[ifd].offset = len > 1 ? ftell(GLOBAL_IO_ifp) : read4bytes();
                if ( len == 1 ) {
                    ifdArray[ifd].tile_width = ifdArray[ifd].tile_length = 0;
                }
                if ( len == 4 ) {
                    TIFF_CALLBACK_loadRawData = &sinar_4shot_load_raw;
                    is_raw = 5;
                }
                break;
            case 330:
                // SubIFDs
                if ( !strcmp(GLOBAL_model, "DSLR-A100") && ifdArray[ifd].width == 3872 ) {
                    TIFF_CALLBACK_loadRawData = &sony_arw_load_raw;
                    GLOBAL_IO_profileOffset = read4bytes() + base;
                    ifd++;
                    break;
                }
                while ( len-- ) {
                    i = ftell(GLOBAL_IO_ifp);
                    fseek(GLOBAL_IO_ifp, read4bytes() + base, SEEK_SET);
                    if ( parse_tiff_ifd(base) ) {
                        break;
                    }
                    fseek(GLOBAL_IO_ifp, i + 4, SEEK_SET);
                }
                break;
            case 400:
                strcpy(GLOBAL_make, "Sarnoff");
                ADOBE_maximum = 0xfff;
                break;
            case 28688:
                for ( c = 0; c < 4; c++ ) {
                    sony_curve[c + 1] = read2bytes() >> 2 & 0xfff;
                }
                for ( i = 0; i < 5; i++ ) {
                    for ( j = sony_curve[i] + 1; j <= sony_curve[i + 1]; j++ ) {
                        GAMMA_curveFunctionLookupTable[j] = GAMMA_curveFunctionLookupTable[j - 1] + (1 << i);
                    }
                }
                break;
            case 29184:
                sony_offset = read4bytes();
                break;
            case 29185:
                sony_length = read4bytes();
                break;
            case 29217:
                sony_key = read4bytes();
                break;
            case 29264:
                parse_minolta(ftell(GLOBAL_IO_ifp));
                THE_image.width = 0;
                break;
            case 29443:
                for ( c = 0; c < 4; c++ ) {
                    GLOBAL_cam_mul[c ^ (c < 2)] = read2bytes();
                }
                break;
            case 29459:
                for ( c = 0; c < 4; c++ ) {
                    GLOBAL_cam_mul[c] = read2bytes();
                }
                i = (GLOBAL_cam_mul[1] == 1024 && GLOBAL_cam_mul[2] == 1024) << 1;
                SWAP(GLOBAL_cam_mul[i], GLOBAL_cam_mul[i + 1])
                break;
            case 33405:
                // Model2
                fgets(model2, 64, GLOBAL_IO_ifp);
                break;
            case 33421:
                // CFARepeatPatternDim
                if ( read2bytes() == 6 && read2bytes() == 6 ) {
                    IMAGE_filters = 9;
                }
                break;
            case 33422:
                // CFAPattern
                if ( IMAGE_filters == 9 ) {
                    for ( c = 0; c < 36; c++ ) {
                        ((char *) xtrans)[c] = fgetc(GLOBAL_IO_ifp) & 3;
                    }
                    break;
                }
            case 64777:
                // Kodak P-series
                if ( (plen = len) > 16 ) {
                    plen = 16;
                }
                fread(cfa_pat, 1, plen, GLOBAL_IO_ifp);
                for ( IMAGE_colors = cfa = i = 0; i < plen && IMAGE_colors < 4; i++ ) {
                    IMAGE_colors += !(cfa & (1 << cfa_pat[i]));
                    cfa |= 1 << cfa_pat[i];
                }
                if ( cfa == 070 ) {
                    memcpy(cfa_pc, "\003\004\005", 3);    /* CMY */
                }
                if ( cfa == 072 ) {
                    memcpy(cfa_pc, "\005\003\004\001", 4);    /* GMCY */
                }
                goto guess_cfa_pc;
            case 33424:
            case 65024:
                fseek(GLOBAL_IO_ifp, read4bytes() + base, SEEK_SET);
                parse_kodak_ifd(base);
                break;
            case 33434:
                // ExposureTime
                ifdArray[ifd].shutter = CAMERA_IMAGE_information.shutterSpeed = readDouble(type);
                break;
            case 33437:
                // FNumber
                aperture = readDouble(type);
                break;
            case 34306:
                // Leaf white balance
                for ( c = 0; c < 4; c++ ) {
                    GLOBAL_cam_mul[c ^ 1] = 4096.0 / read2bytes();
                }
                break;
            case 34307:
                // Leaf CatchLight color matrix
                fread(software, 1, 7, GLOBAL_IO_ifp);
                if ( strncmp(software, "MATRIX", 6)) break;
                IMAGE_colors = 4;
                for ( GLOBAL_colorTransformForRaw = i = 0; i < 3; i++ ) {
                    for ( c = 0; c < 4; c++ ) {
                        fscanf(GLOBAL_IO_ifp, "%f", &rgb_cam[i][c ^ 1]);
                    }
                    if ( !OPTIONS_values->useCameraWb ) {
                        continue;
                    }
                    num = 0;
                    for ( c = 0; c < 4; c++ ) {
                        num += rgb_cam[i][c];
                    }
                    for ( c = 0; c < 4; c++ ) {
                        rgb_cam[i][c] /= num;
                    }
                }
                break;
            case 34310:
                // Leaf metadata
                parse_mos(ftell(GLOBAL_IO_ifp));
            case 34303:
                strcpy(GLOBAL_make, "Leaf");
                break;
            case 34665:
                // EXIF tag
                fseek(GLOBAL_IO_ifp, read4bytes() + base, SEEK_SET);
                parse_exif(base);
                break;
            case 34853:
                // GPSInfo tag
                fseek(GLOBAL_IO_ifp, read4bytes() + base, SEEK_SET);
                parse_gps(base);
                break;
            case 34675:
                // InterColorProfile
            case 50831:
                // AsShotICCProfile
                profile_offset = ftell(GLOBAL_IO_ifp);
                profile_length = len;
                break;
            case 37122:
                // CompressedBitsPerPixel
                kodak_cbpp = read4bytes();
                break;
            case 37386:
                // FocalLength
                focal_len = readDouble(type);
                break;
            case 37393:
                // ImageNumber
                shot_order = readInt(type);
                break;
            case 37400:
                // Old Kodak KDC tag
                for ( GLOBAL_colorTransformForRaw = i = 0; i < 3; i++ ) {
                    readDouble(type);
                    for ( c = 0; c < 3; c++ ) {
                        rgb_cam[i][c] = readDouble(type);
                    };
                }
                break;
            case 40976:
                strip_offset = read4bytes();
                switch ( ifdArray[ifd].comp ) {
                    case 32770:
                        TIFF_CALLBACK_loadRawData = &samsung_load_raw;
                        break;
                    case 32772:
                        TIFF_CALLBACK_loadRawData = &samsung2_load_raw;
                        break;
                    case 32773:
                        TIFF_CALLBACK_loadRawData = &samsung3_load_raw;
                        break;
                }
                break;
            case 46275:
                // Imacon tags
                strcpy(GLOBAL_make, "Imacon");
                GLOBAL_IO_profileOffset = ftell(GLOBAL_IO_ifp);
                ima_len = len;
                break;
            case 46279:
                if ( !ima_len ) {
                    break;
                }
                fseek(GLOBAL_IO_ifp, 38, SEEK_CUR);
            case 46274:
                fseek(GLOBAL_IO_ifp, 40, SEEK_CUR);
                THE_image.width = read4bytes();
                THE_image.height = read4bytes();
                left_margin = read4bytes() & 7;
                width = THE_image.width - left_margin - (read4bytes() & 7);
                top_margin = read4bytes() & 7;
                height = THE_image.height - top_margin - (read4bytes() & 7);
                if ( THE_image.width == 7262 ) {
                    height = 5444;
                    width = 7244;
                    left_margin = 7;
                }
                fseek(GLOBAL_IO_ifp, 52, SEEK_CUR);
                for ( c = 0; c < 3; c++ ) {
                    GLOBAL_cam_mul[c] = readDouble(11);
                }
                fseek(GLOBAL_IO_ifp, 114, SEEK_CUR);
                GLOBAL_flipsMask = (read2bytes() >> 7) * 90;
                if ( width * height * 6 == ima_len ) {
                    if ( GLOBAL_flipsMask % 180 == 90 ) {
                        SWAP(width, height);
                    }
                    THE_image.width = width;
                    THE_image.height = height;
                    left_margin = top_margin = IMAGE_filters = GLOBAL_flipsMask = 0;
                }
                snprintf(GLOBAL_model, 64, "Ixpress %d-Mp", height * width / 1000000);
                TIFF_CALLBACK_loadRawData = &imacon_full_load_raw;
                if ( IMAGE_filters ) {
                    if ( left_margin & 1 ) {
                        IMAGE_filters = 0x61616161;
                    }
                    TIFF_CALLBACK_loadRawData = &unpacked_load_raw;
                }
                ADOBE_maximum = 0xffff;
                break;
            case 50454:
                // Sinar tag
            case 50455:
                if ( !(cbuf = (char *) malloc(len)) ) {
                    break;
                }
                fread(cbuf, 1, len, GLOBAL_IO_ifp);
                for ( cp = cbuf - 1; cp && cp < cbuf + len; cp = strchr(cp, '\n') ) {
                    if ( !strncmp(++cp, "Neutral ", 8)) {
                        sscanf(cp + 8, "%f %f %f", GLOBAL_cam_mul, GLOBAL_cam_mul + 1, GLOBAL_cam_mul + 2);
                    }
                }
                free(cbuf);
                break;
            case 50458:
                if ( !GLOBAL_make[0] ) {
                    strcpy(GLOBAL_make, "Hasselblad");
                }
                break;
            case 50459:
                // Hasselblad tag
                i = GLOBAL_endianOrder;
                j = ftell(GLOBAL_IO_ifp);
                c = numberOfRawImages;
                GLOBAL_endianOrder = read2bytes();
                fseek(GLOBAL_IO_ifp, j + (read2bytes(), read4bytes()), SEEK_SET);
                parse_tiff_ifd(j);
                ADOBE_maximum = 0xffff;
                numberOfRawImages = c;
                GLOBAL_endianOrder = i;
                break;
            case 50706:
                // DNGVersion
                for ( c = 0; c < 4; c++ ) {
                    GLOBAL_dngVersion = (GLOBAL_dngVersion << 8) + fgetc(GLOBAL_IO_ifp);
                }
                if ( !GLOBAL_make[0] ) {
                    strcpy(GLOBAL_make, "DNG");
                }
                is_raw = 1;
                break;
            case 50708:
                // UniqueCameraModel
                if ( GLOBAL_model[0] ) {
                    break;
                }
                fgets(GLOBAL_make, 64, GLOBAL_IO_ifp);
                if ( (cp = strchr(GLOBAL_make, ' ')) ) {
                    strcpy(GLOBAL_model, cp + 1);
                    *cp = 0;
                }
                break;
            case 50710:
                // CFAPlaneColor
                if ( IMAGE_filters == 9 ) {
                    break;
                }
                if ( len > 4 ) {
                    len = 4;
                }
                IMAGE_colors = len;
                fread(cfa_pc, 1, IMAGE_colors, GLOBAL_IO_ifp);
            guess_cfa_pc:
                for ( c = 0; c < IMAGE_colors; c++ ) {
                    tab[cfa_pc[c]] = c;
                }
                GLOBAL_bayerPatternLabels[c] = 0;
                for ( i = 16; i--; ) {
                    IMAGE_filters = IMAGE_filters << 2 | tab[cfa_pat[i % plen]];
                }
                IMAGE_filters -= !IMAGE_filters;
                break;
            case 50711:
                // CFALayout
                if ( read2bytes() == 2 ) {
                    fuji_width = 1;
                }
                break;
            case 291:
            case 50712:
                // LinearizationTable
                linear_table(len);
                break;
            case 50713:
                // BlackLevelRepeatDim
                cblack[4] = read2bytes();
                cblack[5] = read2bytes();
                if ( cblack[4] * cblack[5] > sizeof cblack / sizeof *cblack - 6 ) {
                    cblack[4] = cblack[5] = 1;
                }
                break;
            case 61450:
                cblack[4] = cblack[5] = MIN(sqrt(len), 64);
            case 50714:            /* BlackLevel */
                if ( !(cblack[4] * cblack[5]) ) {
                    cblack[4] = cblack[5] = 1;
                }
                for ( c = 0; c < cblack[4] * cblack[5]; c++ ) {
                    cblack[6 + c] = readDouble(type);
                }
                ADOBE_black = 0;
                break;
            case 50715:
                // BlackLevelDeltaH
            case 50716:
                // BlackLevelDeltaV
                for ( num = i = 0; i < (len & 0xffff); i++ ) {
                    num += readDouble(type);
                }
                ADOBE_black += num / len + 0.5;
                break;
            case 50717:
                // WhiteLevel
                ADOBE_maximum = readInt(type);
                break;
            case 50718:
                // DefaultScale
                pixel_aspect = readDouble(type);
                pixel_aspect /= readDouble(type);
                break;
            case 50721:
                // ColorMatrix1
            case 50722:
                // ColorMatrix2
                for ( c = 0; c < IMAGE_colors; c++ ) {
                    for ( j = 0; j < 3; j++ ) {
                        cm[c][j] = readDouble(type);
                    }
                }
                use_cm = 1;
                break;
            case 50723:
                // CameraCalibration1
            case 50724:
                // CameraCalibration2
                for ( i = 0; i < IMAGE_colors; i++ ) {
                    for ( c = 0; c < IMAGE_colors; c++ ) {
                        cc[i][c] = readDouble(type);
                    }
                }
                break;
            case 50727:
                // AnalogBalance
                for ( c = 0; c < IMAGE_colors; c++ ) {
                    ab[c] = readDouble(type);
                }
                break;
            case 50728:
                // AsShotNeutral
                for ( c = 0; c < IMAGE_colors; c++ ) {
                    asn[c] = readDouble(type);
                }
                break;
            case 50729:
                // AsShotWhiteXY
                xyz[0] = readDouble(type);
                xyz[1] = readDouble(type);
                xyz[2] = 1 - xyz[0] - xyz[1];
                for ( c = 0; c < 3; c++ ) {
                    xyz[c] /= d65_white[c];
                };
                break;
            case 50740:
                // DNGPrivateData
                if ( GLOBAL_dngVersion ) {
                    break;
                }
                parse_minolta(j = read4bytes() + base);
                fseek(GLOBAL_IO_ifp, j, SEEK_SET);
                parse_tiff_ifd(base);
                break;
            case 50752:
                readShorts(cr2_slice, 3);
                break;
            case 50829:
                // ActiveArea
                top_margin = readInt(type);
                left_margin = readInt(type);
                height = readInt(type) - top_margin;
                width = readInt(type) - left_margin;
                break;
            case 50830:
                // MaskedAreas
                for ( i = 0; i < len && i < 32; i++ ) {
                    ((int *) mask)[i] = readInt(type);
                }
                ADOBE_black = 0;
                break;
            case 51009:
                // OpcodeList2
                GLOBAL_meta_offset = ftell(GLOBAL_IO_ifp);
                break;
            case 64772:
                // Kodak P-series
                if ( len < 13 ) {
                    break;
                }
                fseek(GLOBAL_IO_ifp, 16, SEEK_CUR);
                GLOBAL_IO_profileOffset = read4bytes();
                fseek(GLOBAL_IO_ifp, 28, SEEK_CUR);
                GLOBAL_IO_profileOffset += read4bytes();
                TIFF_CALLBACK_loadRawData = &packed_load_raw;
                break;
            case 65026:
                if ( type == 2 ) {
                    fgets(model2, 64, GLOBAL_IO_ifp);
                }
        }
        fseek(GLOBAL_IO_ifp, save, SEEK_SET);
    }

    if ( sony_length && (buf = (unsigned *) malloc(sony_length)) ) {
        fseek(GLOBAL_IO_ifp, sony_offset, SEEK_SET);
        fread(buf, sony_length, 1, GLOBAL_IO_ifp);
        sony_decrypt(buf, sony_length / 4, 1, sony_key);
        sfp = GLOBAL_IO_ifp;
        if ( (GLOBAL_IO_ifp = tmpfile()) ) {
            fwrite(buf, sony_length, 1, GLOBAL_IO_ifp);
            fseek(GLOBAL_IO_ifp, 0, SEEK_SET);
            parse_tiff_ifd(-sony_offset);
            fclose(GLOBAL_IO_ifp);
        }
        GLOBAL_IO_ifp = sfp;
        free(buf);
    }
    for ( i = 0; i < IMAGE_colors; i++ ) {
        for ( c = 0; c < IMAGE_colors; c++ ) {
            cc[i][c] *= ab[i];
        }
    }
    if ( use_cm ) {
        for ( c = 0; c < IMAGE_colors; c++ ) {
            for ( i = 0; i < 3; i++ ) {
                for ( cam_xyz[c][i] = j = 0; j < IMAGE_colors; j++ ) {
                    cam_xyz[c][i] += cc[c][j] * cm[j][i] * xyz[i];
                }
            }
        }
        cam_xyz_coeff(cmatrix, cam_xyz);
    }
    if ( asn[0] ) {
        GLOBAL_cam_mul[3] = 0;
        for ( c = 0; c < IMAGE_colors; c++ ) {
            GLOBAL_cam_mul[c] = 1 / asn[c];
        };
    }
    if ( !use_cm ) {
        for ( c = 0; c < IMAGE_colors; c++ ) {
            pre_mul[c] /= cc[c][c];
        }
    }
    return 0;
}

int
parse_tiff(int base) {
    int doff;

    fseek(GLOBAL_IO_ifp, base, SEEK_SET);
    GLOBAL_endianOrder = read2bytes();
    if ( GLOBAL_endianOrder != LITTLE_ENDIAN_ORDER && GLOBAL_endianOrder != BIG_ENDIAN_ORDER ) {
        return 0;
    }
    read2bytes();
    while ( (doff = read4bytes()) ) {
        fseek(GLOBAL_IO_ifp, doff + base, SEEK_SET);
        if ( parse_tiff_ifd(base)) {
            break;
        }
    }
    return 1;
}

void
apply_tiff() {
    int max_samp = 0;
    int ties = 0;
    int os;
    int ns;
    int raw = -1;
    int thm = -1;
    int i;
    struct jhead jh;

    thumb_misc = 16;
    if ( thumb_offset ) {
        fseek(GLOBAL_IO_ifp, thumb_offset, SEEK_SET);
        if ( ljpeg_start(&jh, 1)) {
            thumb_misc = jh.bits;
            thumb_width = jh.wide;
            thumb_height = jh.high;
        }
    }
    for ( i = numberOfRawImages; i--; ) {
        if ( ifdArray[i].shutter ) {
            CAMERA_IMAGE_information.shutterSpeed = ifdArray[i].shutter;
        }
        ifdArray[i].shutter = CAMERA_IMAGE_information.shutterSpeed;
    }
    for ( i = 0; i < numberOfRawImages; i++ ) {
        if ( max_samp < ifdArray[i].samples ) {
            max_samp = ifdArray[i].samples;
        }
        if ( max_samp > 3 ) {
            max_samp = 3;
        }
        os = THE_image.width * THE_image.height;
        ns = ifdArray[i].width * ifdArray[i].height;
        if ( THE_image.bitsPerSample ) {
            os *= THE_image.bitsPerSample;
            ns *= ifdArray[i].bps;
        }
        if ((ifdArray[i].comp != 6 || ifdArray[i].samples != 3) &&
            (ifdArray[i].width | ifdArray[i].height) < 0x10000 &&
            ns && ((ns > os && (ties = 1)) ||
                   (ns == os && OPTIONS_values->shotSelect == ties++))) {
            THE_image.width = ifdArray[i].width;
            THE_image.height = ifdArray[i].height;
            THE_image.bitsPerSample = ifdArray[i].bps;
            tiff_compress = ifdArray[i].comp;
            GLOBAL_IO_profileOffset = ifdArray[i].offset;
            cameraFlip = ifdArray[i].flip;
            tiff_samples = ifdArray[i].samples;
            tile_width = ifdArray[i].tile_width;
            tile_length = ifdArray[i].tile_length;
            CAMERA_IMAGE_information.shutterSpeed = ifdArray[i].shutter;
            raw = i;
        }
    }
    if ( is_raw == 1 && ties ) {
        is_raw = ties;
    }
    if ( !tile_width ) {
        tile_width = INT_MAX;
    }
    if ( !tile_length ) {
        tile_length = INT_MAX;
    }
    for ( i = numberOfRawImages; i--; ) {
        if ( ifdArray[i].flip ) {
            cameraFlip = ifdArray[i].flip;
        }
    }
    if ( raw >= 0 && !TIFF_CALLBACK_loadRawData ) {
        switch ( tiff_compress ) {
            case 32767:
                if ( ifdArray[raw].bytes == THE_image.width * THE_image.height ) {
                    THE_image.bitsPerSample = 12;
                    ADOBE_maximum = 4095;
                    TIFF_CALLBACK_loadRawData = &sony_arw2_load_raw;
                    break;
                }
                if ( ifdArray[raw].bytes * 8 != THE_image.width * THE_image.height * THE_image.bitsPerSample ) {
                    THE_image.height += 8;
                    TIFF_CALLBACK_loadRawData = &sony_arw_load_raw;
                    break;
                }
                GLOBAL_loadFlags = 79;
            case 32769:
                GLOBAL_loadFlags++;
            case 32770:
            case 32773:
                goto slr;
            case 0:
            case 1:
                if ( !strncmp(GLOBAL_make, "OLYMPUS", 7) &&
                     ifdArray[raw].bytes * 2 == THE_image.width * THE_image.height * 3 ) {
                    GLOBAL_loadFlags = 24;
                }
                if ( !strcmp(GLOBAL_make, "SONY") && THE_image.bitsPerSample < 14 &&
                     ifdArray[raw].bytes == THE_image.width * THE_image.height * 2 ) {
                    THE_image.bitsPerSample = 14;
                }
                if ( ifdArray[raw].bytes * 5 == THE_image.width * THE_image.height * 8 ) {
                    GLOBAL_loadFlags = 81;
                    THE_image.bitsPerSample = 12;
                }
            slr:
                switch ( THE_image.bitsPerSample ) {
                    case 8:
                        TIFF_CALLBACK_loadRawData = &eight_bit_load_raw;
                        break;
                    case 12:
                        if ( ifdArray[raw].phint == 2 )
                            GLOBAL_loadFlags = 6;
                        TIFF_CALLBACK_loadRawData = &packed_load_raw;
                        break;
                    case 14:
                        TIFF_CALLBACK_loadRawData = &packed_load_raw;
                        if ( ifdArray[raw].bytes * 4 == THE_image.width * THE_image.height * 7 ) break;
                        GLOBAL_loadFlags = 0;
                    case 16:
                        TIFF_CALLBACK_loadRawData = &unpacked_load_raw;
                        if ( !strncmp(GLOBAL_make, "OLYMPUS", 7) &&
                             ifdArray[raw].bytes * 7 > THE_image.width * THE_image.height )
                            TIFF_CALLBACK_loadRawData = &olympus_load_raw;
                }
                if ( IMAGE_filters == 9 && ifdArray[raw].bytes * 8 < THE_image.width * THE_image.height * THE_image.bitsPerSample ) {
                    TIFF_CALLBACK_loadRawData = &fuji_xtrans_load_raw;
                }
                break;
            case 6:
            case 7:
            case 99:
                TIFF_CALLBACK_loadRawData = &lossless_jpeg_load_raw;
                break;
            case 262:
                TIFF_CALLBACK_loadRawData = &kodak_262_load_raw;
                break;
            case 34713:
                if ((THE_image.width + 9) / 10 * 16 * THE_image.height == ifdArray[raw].bytes ) {
                    TIFF_CALLBACK_loadRawData = &packed_load_raw;
                    GLOBAL_loadFlags = 1;
                } else {
                    if ( THE_image.width * THE_image.height * 3 == ifdArray[raw].bytes * 2 ) {
                        TIFF_CALLBACK_loadRawData = &packed_load_raw;
                        if ( GLOBAL_model[0] == 'N' ) {
                            GLOBAL_loadFlags = 80;
                        }
                    } else {
                        if ( THE_image.width * THE_image.height * 3 == ifdArray[raw].bytes ) {
                            TIFF_CALLBACK_loadRawData = &nikon_yuv_load_raw;
                            gamma_curve(1 / 2.4, 12.92, 1, 4095);
                            memset(cblack, 0, sizeof cblack);
                            IMAGE_filters = 0;
                        } else {
                            if ( THE_image.width * THE_image.height * 2 == ifdArray[raw].bytes ) {
                                TIFF_CALLBACK_loadRawData = &unpacked_load_raw;
                                GLOBAL_loadFlags = 4;
                                GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
                            } else {
                                TIFF_CALLBACK_loadRawData = &nikon_load_raw;
                            }
                        }
                    }
                }
                break;
            case 65535:
                TIFF_CALLBACK_loadRawData = &pentax_load_raw;
                break;
            case 65000:
                switch ( ifdArray[raw].phint ) {
                    case 2:
                        TIFF_CALLBACK_loadRawData = &kodak_rgb_load_raw;
                        IMAGE_filters = 0;
                        break;
                    case 6:
                        TIFF_CALLBACK_loadRawData = &kodak_ycbcr_load_raw;
                        IMAGE_filters = 0;
                        break;
                    case 32803:
                        TIFF_CALLBACK_loadRawData = &kodak_65000_load_raw;
                }
            case 32867:
            case 34892:
                break;
            default:
                is_raw = 0;
        }
    }
    if ( !GLOBAL_dngVersion ) {
        if ((tiff_samples == 3 && ifdArray[raw].bytes && THE_image.bitsPerSample != 14 &&
             (tiff_compress & -16) != 32768)
            || (THE_image.bitsPerSample == 8 && strncmp(GLOBAL_make, "Phase", 5) &&
                !strcasestr(GLOBAL_make, "Kodak") && !strstr(model2, "DEBUG RAW"))) {
            is_raw = 0;
        }
    }

    for ( i = 0; i < numberOfRawImages; i++ ) {
        if ( i != raw && ifdArray[i].samples == max_samp &&
             ifdArray[i].width * ifdArray[i].height / (SQR(ifdArray[i].bps) + 1) >
             thumb_width * thumb_height / (SQR(thumb_misc) + 1)
             && ifdArray[i].comp != 34892 ) {
            thumb_width = ifdArray[i].width;
            thumb_height = ifdArray[i].height;
            thumb_offset = ifdArray[i].offset;
            thumb_length = ifdArray[i].bytes;
            thumb_misc = ifdArray[i].bps;
            thm = i;
        }
    }

    if ( thm >= 0 ) {
        thumb_misc |= ifdArray[thm].samples << 5;
        switch ( ifdArray[thm].comp ) {
            case 0:
                write_thumb = &layer_thumb;
                break;
            case 1:
                if ( ifdArray[thm].bps <= 8 ) {
                    write_thumb = &ppm_thumb;
                } else {
                    if ( !strcmp(GLOBAL_make, "Imacon") ) {
                        write_thumb = &ppm16_thumb;
                    } else {
                        CALLBACK_loadThumbnailRawData = &kodak_thumb_load_raw;
                    }
                }
                break;
            case 65000:
                CALLBACK_loadThumbnailRawData = ifdArray[thm].phint == 6 ?
                                                &kodak_ycbcr_load_raw : &kodak_rgb_load_raw;
        }
    }
}

void
parse_minolta(int base) {
    int save;
    int tag;
    int len;
    int offset;
    int high = 0;
    int wide = 0;
    int i;
    int c;
    short sorder = GLOBAL_endianOrder;

    fseek(GLOBAL_IO_ifp, base, SEEK_SET);
    if ( fgetc(GLOBAL_IO_ifp) || fgetc(GLOBAL_IO_ifp) - 'M' || fgetc(GLOBAL_IO_ifp) - 'R' ) {
        return;
    }
    GLOBAL_endianOrder = fgetc(GLOBAL_IO_ifp) * 0x101;
    offset = base + read4bytes() + 8;
    while ((save = ftell(GLOBAL_IO_ifp)) < offset ) {
        for ( tag = i = 0; i < 4; i++ ) {
            tag = tag << 8 | fgetc(GLOBAL_IO_ifp);
        }
        len = read4bytes();
        switch ( tag ) {
            case 0x505244:
                // PRD
                fseek(GLOBAL_IO_ifp, 8, SEEK_CUR);
                high = read2bytes();
                wide = read2bytes();
                break;
            case 0x574247:
                // WBG
                read4bytes();
                i = strcmp(GLOBAL_model, "DiMAGE A200") ? 0 : 3;
                for ( c = 0; c < 4; c++ ) {
                    GLOBAL_cam_mul[c ^ (c >> 1) ^ i] = read2bytes();
                };
                break;
            case 0x545457:
                // TTW
                parse_tiff(ftell(GLOBAL_IO_ifp));
                GLOBAL_IO_profileOffset = offset;
        }
        fseek(GLOBAL_IO_ifp, save + len + 8, SEEK_SET);
    }
    THE_image.height = high;
    THE_image.width = wide;
    GLOBAL_endianOrder = sorder;
}

/*
Many cameras have a "debug mode" that writes JPEG and raw
at the same time.  The raw file has no header, so try to
open the matching JPEG file and read its metadata.
*/
void
parse_external_jpeg() {
    const char *file;
    const char *ext;
    char *jname;
    char *jfile;
    char *jext;
    FILE *save = GLOBAL_IO_ifp;

    ext = strrchr(CAMERA_IMAGE_information.inputFilename, '.');
    file = strrchr(CAMERA_IMAGE_information.inputFilename, '/');
    if ( !file ) {
        file = strrchr(CAMERA_IMAGE_information.inputFilename, '\\');
    }
    if ( !file ) {
        file = CAMERA_IMAGE_information.inputFilename - 1;
    }
    file++;
    if ( !ext || strlen(ext) != 4 || ext - file != 8 ) {
        return;
    }
    jname = (char *) malloc(strlen(CAMERA_IMAGE_information.inputFilename) + 1);
    memoryError(jname, "parse_external_jpeg()");
    strcpy(jname, CAMERA_IMAGE_information.inputFilename);
    jfile = file - CAMERA_IMAGE_information.inputFilename + jname;
    jext = ext - CAMERA_IMAGE_information.inputFilename + jname;
    if ( strcasecmp(ext, ".jpg") ) {
        strcpy(jext, isupper(ext[1]) ? ".JPG" : ".jpg");
        if ( isdigit(*file)) {
            memcpy(jfile, file + 4, 4);
            memcpy(jfile + 4, file, 4);
        }
    } else {
        while ( isdigit(*--jext)) {
            if ( *jext != '9' ) {
                (*jext)++;
                break;
            }
            *jext = '0';
        }
    }
    if ( strcmp(jname, CAMERA_IMAGE_information.inputFilename) ) {
        if ( (GLOBAL_IO_ifp = fopen(jname, "rb")) ) {
            if ( OPTIONS_values->verbose ) {
                fprintf(stderr, _("Reading metadata from %s ...\n"), jname);
            }
            parse_tiff(12);
            thumb_offset = 0;
            is_raw = 1;
            fclose(GLOBAL_IO_ifp);
        }
    }
    if ( !timestamp ) {
        fprintf(stderr, _("Failed to read metadata from %s\n"), jname);
    }
    free(jname);
    GLOBAL_IO_ifp = save;
}

/*
CIFF block 0x1030 contains 8x8 white sample.
Load this into white[][] for use in scale_colors().
*/
void
ciff_block_1030() {
    static const unsigned short key[] = {0x410, 0x45f3};
    int i;
    int bpp;
    int row;
    int col;
    int vbits = 0;
    unsigned long bitbuf = 0;

    if ((read2bytes(), read4bytes()) != 0x80008 || !read4bytes() ) {
        return;
    }
    bpp = read2bytes();
    if ( bpp != 10 && bpp != 12 ) {
        return;
    }
    for ( i = row = 0; row < 8; row++ ) {
        for ( col = 0; col < 8; col++ ) {
            if ( vbits < bpp ) {
                bitbuf = bitbuf << 16 | (read2bytes() ^ key[i++ & 1]);
                vbits += 16;
            }
            white[row][col] = bitbuf >> (vbits -= bpp) & ~(-1 << bpp);
        }
    }
}

/*
Parse a CIFF file, better known as Canon CRW format.
*/
void
parse_ciff(int offset, int length, int depth) {
    int tboff;
    int nrecs;
    int c;
    int type;
    int len;
    int save;
    int wbi = -1;
    unsigned short key[] = {0x410, 0x45f3};

    fseek(GLOBAL_IO_ifp, offset + length - 4, SEEK_SET);
    tboff = read4bytes() + offset;
    fseek(GLOBAL_IO_ifp, tboff, SEEK_SET);
    nrecs = read2bytes();
    if ( (nrecs | depth) > 127 ) {
        return;
    }

    while ( nrecs-- ) {
        type = read2bytes();
        len = read4bytes();
        save = ftell(GLOBAL_IO_ifp) + 4;
        fseek(GLOBAL_IO_ifp, offset + read4bytes(), SEEK_SET);

        if ( (((type >> 8) + 8) | 8) == 0x38 ) {
            // Parse a sub-table
            parse_ciff(ftell(GLOBAL_IO_ifp), len, depth + 1);
        }

        if ( type == 0x0810 ) {
            fread(artist, 64, 1, GLOBAL_IO_ifp);
        }

        if ( type == 0x080a ) {
            fread(GLOBAL_make, 64, 1, GLOBAL_IO_ifp);
            fseek(GLOBAL_IO_ifp, strlen(GLOBAL_make) - 63, SEEK_CUR);
            fread(GLOBAL_model, 64, 1, GLOBAL_IO_ifp);
        }

        if ( type == 0x1810 ) {
            width = read4bytes();
            height = read4bytes();
            pixel_aspect = intToFloat(read4bytes());
            GLOBAL_flipsMask = read4bytes();
        }

        if ( type == 0x1835 ) {
            // Get the decoder table
            tiff_compress = read4bytes();
        }

        if ( type == 0x2007 ) {
            thumb_offset = ftell(GLOBAL_IO_ifp);
            thumb_length = len;
        }

        if ( type == 0x1818 ) {
            CAMERA_IMAGE_information.shutterSpeed = pow(2, -intToFloat((read4bytes(), read4bytes())));
            aperture = pow(2, intToFloat(read4bytes()) / 2);
        }

        if ( type == 0x102a ) {
            iso_speed = pow(2, (read4bytes(), read2bytes()) / 32.0 - 4) * 50;
            aperture = pow(2, (read2bytes(), (short) read2bytes()) / 64.0);
            CAMERA_IMAGE_information.shutterSpeed = pow(2, -((short) read2bytes()) / 32.0);
            wbi = (read2bytes(), read2bytes());
            if ( wbi > 17 ) {
                wbi = 0;
            }
            fseek(GLOBAL_IO_ifp, 32, SEEK_CUR);
            if ( CAMERA_IMAGE_information.shutterSpeed > 1e6 ) {
                CAMERA_IMAGE_information.shutterSpeed = read2bytes() / 10.0;
            }
        }

        if ( type == 0x102c ) {
            if ( read2bytes() > 512 ) {
                // Pro90, G1
                fseek(GLOBAL_IO_ifp, 118, SEEK_CUR);
                for ( c = 0; c < 4; c++ ) {
                    GLOBAL_cam_mul[c ^ 2] = read2bytes();
                }
            } else {
                // G2, S30, S40
                fseek(GLOBAL_IO_ifp, 98, SEEK_CUR);
                for ( c = 0; c < 4; c++ ) {
                    GLOBAL_cam_mul[c ^ (c >> 1) ^ 1] = read2bytes();
                }
            }
        }

        if ( type == 0x0032 ) {
            if ( len == 768 ) {
                // EOS D30
                fseek(GLOBAL_IO_ifp, 72, SEEK_CUR);
                for ( c = 0; c < 4; c++ ) {
                    GLOBAL_cam_mul[c ^ (c >> 1)] = 1024.0 / read2bytes();
                }
                if ( !wbi ) {
                    // use my auto white balance
                    GLOBAL_cam_mul[0] = -1;
                }
            } else {
                if ( !GLOBAL_cam_mul[0] ) {
                    if ( read2bytes() == key[0] ) {
                        // Pro1, G6, S60, S70
                        c = (strstr(GLOBAL_model, "Pro1") ?
                             "012346000000000000" : "01345:000000006008")[wbi] - '0' + 2;
                    } else {                /* G3, G5, S45, S50 */
                        c = "023457000000006000"[wbi] - '0';
                        key[0] = key[1] = 0;
                    }
                    fseek(GLOBAL_IO_ifp, 78 + c * 8, SEEK_CUR);
                    for ( c = 0; c < 4; c++ ) {
                        GLOBAL_cam_mul[c ^ (c >> 1) ^ 1] = read2bytes() ^ key[c & 1];
                    }
                    if ( !wbi ) {
                        GLOBAL_cam_mul[0] = -1;
                    }
                }
            }
        }

        if ( type == 0x10a9 ) {
            // D60, 10D, 300D, and clones
            if ( len > 66 ) {
                wbi = "0134567028"[wbi] - '0';
            }
            fseek(GLOBAL_IO_ifp, 2 + wbi * 8, SEEK_CUR);
            for ( c = 0; c < 4; c++ ) {
                GLOBAL_cam_mul[c ^ (c >> 1)] = read2bytes();
            }
        }

        if ( type == 0x1030 && (0x18040 >> wbi & 1) ) {
            // All that don't have 0x10a9
            ciff_block_1030();
        }

        if ( type == 0x1031 ) {
            THE_image.width = (read2bytes(), read2bytes());
            THE_image.height = read2bytes();
        }

        if ( type == 0x5029 ) {
            focal_len = len >> 16;
            if ( (len & 0xffff) == 2 ) {
                focal_len /= 32;
            }
        }

        if ( type == 0x5813 ) {
            flash_used = intToFloat(len);
        }

        if ( type == 0x5814 ) {
            canon_ev = intToFloat(len);
        }

        if ( type == 0x5817 ) {
            shot_order = len;
        }

        if ( type == 0x5834 ) {
            unique_id = len;
        }

        if ( type == 0x580e ) {
            timestamp = len;
        }

        if ( type == 0x180e ) {
            timestamp = read4bytes();
        }

#ifdef LOCALTIME
    if ( (type | 0x4000) == 0x580e ) {
        timestamp = mktime (gmtime (&timestamp));
    }
#endif
        fseek(GLOBAL_IO_ifp, save, SEEK_SET);
    }
}

void
parse_rollei() {
    char line[128];
    char *val;
    struct tm t;

    fseek(GLOBAL_IO_ifp, 0, SEEK_SET);
    memset(&t, 0, sizeof t);
    do {
        fgets(line, 128, GLOBAL_IO_ifp);
        if ( (val = strchr(line, '=')) ) {
            *val++ = 0;
        } else {
            val = line + strlen(line);
        }

        if ( !strcmp(line, "DAT") ) {
            sscanf(val, "%d.%d.%d", &t.tm_mday, &t.tm_mon, &t.tm_year);
        }

        if ( !strcmp(line, "TIM") ) {
            sscanf(val, "%d:%d:%d", &t.tm_hour, &t.tm_min, &t.tm_sec);
        }

        if ( !strcmp(line, "HDR") ) {
            thumb_offset = atoi(val);
        }

        if ( !strcmp(line, "X  ") ) {
            THE_image.width = atoi(val);
        }

        if ( !strcmp(line, "Y  ") ) {
            THE_image.height = atoi(val);
        }

        if ( !strcmp(line, "TX ") ) {
            thumb_width = atoi(val);
        }

        if ( !strcmp(line, "TY ") ) {
            thumb_height = atoi(val);
        }
    }
    while ( strncmp(line, "EOHD", 4) );
    GLOBAL_IO_profileOffset = thumb_offset + thumb_width * thumb_height * 2;
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    if ( mktime(&t) > 0 ) {
        timestamp = mktime(&t);
    }
    strcpy(GLOBAL_make, "Rollei");
    strcpy(GLOBAL_model, "d530flex");
    write_thumb = & rollei_thumb;
}

void
parse_sinar_ia() {
    int entries;
    int off;
    char str[8];
    char *cp;

    GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
    fseek(GLOBAL_IO_ifp, 4, SEEK_SET);
    entries = read4bytes();
    fseek(GLOBAL_IO_ifp, read4bytes(), SEEK_SET);
    while ( entries-- ) {
        off = read4bytes();
        read4bytes();
        fread(str, 8, 1, GLOBAL_IO_ifp);
        if ( !strcmp(str, "META") ) {
            GLOBAL_meta_offset = off;
        }
        if ( !strcmp(str, "THUMB") ) {
            thumb_offset = off;
        }
        if ( !strcmp(str, "RAW0") ) {
            GLOBAL_IO_profileOffset = off;
        }
    }
    fseek(GLOBAL_IO_ifp, GLOBAL_meta_offset + 20, SEEK_SET);
    fread(GLOBAL_make, 64, 1, GLOBAL_IO_ifp);
    GLOBAL_make[63] = 0;
    if ( (cp = strchr(GLOBAL_make, ' ')) ) {
        strcpy(GLOBAL_model, cp + 1);
        *cp = 0;
    }
    THE_image.width = read2bytes();
    THE_image.height = read2bytes();
    TIFF_CALLBACK_loadRawData = &unpacked_load_raw;
    thumb_width = (read4bytes(), read2bytes());
    thumb_height = read2bytes();
    write_thumb = &ppm_thumb;
    ADOBE_maximum = 0x3fff;
}

void
parse_phase_one(int base) {
    unsigned entries;
    unsigned tag;
    unsigned type;
    unsigned len;
    unsigned data;
    unsigned save;
    unsigned i;
    unsigned c;
    float romm_cam[3][3];
    char *cp;

    memset(&ph1, 0, sizeof ph1);
    fseek(GLOBAL_IO_ifp, base, SEEK_SET);
    GLOBAL_endianOrder = read4bytes() & 0xffff;
    if ( read4bytes() >> 8 != 0x526177 ) {
        // "Raw"
        return;
    }
    fseek(GLOBAL_IO_ifp, read4bytes() + base, SEEK_SET);
    entries = read4bytes();
    read4bytes();
    while ( entries-- ) {
        tag = read4bytes();
        type = read4bytes();
        len = read4bytes();
        data = read4bytes();
        save = ftell(GLOBAL_IO_ifp);
        fseek(GLOBAL_IO_ifp, base + data, SEEK_SET);
        switch ( tag ) {
            case 0x100:
                GLOBAL_flipsMask = "0653"[data & 3] - '0';
                break;
            case 0x106:
                for ( i = 0; i < 9; i++ ) {
                    ((float *) romm_cam)[i] = readDouble(11);
                }
                romm_coeff(romm_cam);
                break;
            case 0x107:
                for ( c = 0; c < 3; c++ ) {
                    GLOBAL_cam_mul[c] = readDouble(11);
                }
                break;
            case 0x108:
                THE_image.width = data;
                break;
            case 0x109:
                THE_image.height = data;
                break;
            case 0x10a:
                left_margin = data;
                break;
            case 0x10b:
                top_margin = data;
                break;
            case 0x10c:
                width = data;
                break;
            case 0x10d:
                height = data;
                break;
            case 0x10e:
                ph1.format = data;
                break;
            case 0x10f:
                GLOBAL_IO_profileOffset = data + base;
                break;
            case 0x110:
                GLOBAL_meta_offset = data + base;
                meta_length = len;
                break;
            case 0x112:
                ph1.key_off = save - 4;
                break;
            case 0x210:
                ph1.tag_210 = intToFloat(data);
                break;
            case 0x21a:
                ph1.tag_21a = data;
                break;
            case 0x21c:
                strip_offset = data + base;
                break;
            case 0x21d:
                ph1.black = data;
                break;
            case 0x222:
                ph1.split_col = data;
                break;
            case 0x223:
                ph1.black_col = data + base;
                break;
            case 0x224:
                ph1.split_row = data;
                break;
            case 0x225:
                ph1.black_row = data + base;
                break;
            case 0x301:
                GLOBAL_model[63] = 0;
                fread(GLOBAL_model, 1, 63, GLOBAL_IO_ifp);
                if ( (cp = strstr(GLOBAL_model, " camera")) ) {
                    *cp = 0;
                }
                break;
            default:
                break;
        }
        fseek(GLOBAL_IO_ifp, save, SEEK_SET);
    }

    TIFF_CALLBACK_loadRawData = ph1.format < 3 ? &phase_one_load_raw : &phase_one_load_raw_c;
    ADOBE_maximum = 0xffff;
    strcpy(GLOBAL_make, "Phase One");
    if ( GLOBAL_model[0] ) {
        return;
    }
    switch ( THE_image.height ) {
        case 2060:
            strcpy(GLOBAL_model, "LightPhase");
            break;
        case 2682:
            strcpy(GLOBAL_model, "H 10");
            break;
        case 4128:
            strcpy(GLOBAL_model, "H 20");
            break;
        case 5488:
            strcpy(GLOBAL_model, "H 25");
            break;
            break;
        default:
            break;
    }
}

void
parse_fuji(int offset) {
    unsigned entries;
    unsigned tag;
    unsigned len;
    unsigned save;
    unsigned c;

    fseek(GLOBAL_IO_ifp, offset, SEEK_SET);
    entries = read4bytes();
    if ( entries > 255 ) {
        return;
    }
    while ( entries-- ) {
        tag = read2bytes();
        len = read2bytes();
        save = ftell(GLOBAL_IO_ifp);
        if ( tag == 0x100 ) {
            THE_image.height = read2bytes();
            THE_image.width = read2bytes();
        } else {
            if ( tag == 0x121 ) {
                height = read2bytes();
                if ((width = read2bytes()) == 4284 ) {
                    width += 3;
                }
            } else {
                if ( tag == 0x130 ) {
                    fuji_layout = fgetc(GLOBAL_IO_ifp) >> 7;
                    fuji_width = !(fgetc(GLOBAL_IO_ifp) & 8);
                } else {
                    if ( tag == 0x131 ) {
                        IMAGE_filters = 9;
                        for ( c = 0; c < 36; c++ ) {
                            xtrans_abs[0][35 - c] = fgetc(GLOBAL_IO_ifp) & 3;
                        }
                    } else {
                        if ( tag == 0x2ff0 ) {
                            for ( c = 0; c < 4; c++ ) {
                                GLOBAL_cam_mul[c ^ 1] = read2bytes();
                            }
                        } else {
                            if ( tag == 0xc000 && len > 20000 ) {
                                c = GLOBAL_endianOrder;
                                GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
                                while ((tag = read4bytes()) > THE_image.width );
                                width = tag;
                                height = read4bytes();
                                GLOBAL_endianOrder = c;
                            }
                        }
                    }
                }
            }
        }
        fseek(GLOBAL_IO_ifp, save + len, SEEK_SET);
    }
    height <<= fuji_layout;
    width >>= fuji_layout;
}

int
parse_jpeg(int offset) {
    int len;
    int save;
    int hlen;
    int mark;

    fseek(GLOBAL_IO_ifp, offset, SEEK_SET);
    if ( fgetc(GLOBAL_IO_ifp) != 0xff || fgetc(GLOBAL_IO_ifp) != 0xd8 ) {
        return 0;
    }

    while ( fgetc(GLOBAL_IO_ifp) == 0xff && (mark = fgetc(GLOBAL_IO_ifp)) != 0xda ) {
        GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
        len = read2bytes() - 2;
        save = ftell(GLOBAL_IO_ifp);
        if ( mark == 0xc0 || mark == 0xc3 || mark == 0xc9 ) {
                    fgetc(GLOBAL_IO_ifp);
            THE_image.height = read2bytes();
            THE_image.width = read2bytes();
        }
        GLOBAL_endianOrder = read2bytes();
        hlen = read4bytes();
        if ( read4bytes() == 0x48454150 ) {
            // "HEAP"
            parse_ciff(save + hlen, len - hlen, 0);
        }
        if ( parse_tiff(save + 6)) {
            apply_tiff();
        }
        fseek(GLOBAL_IO_ifp, save + len, SEEK_SET);
    }
    return 1;
}

void
parse_riff() {
    unsigned i;
    unsigned size;
    unsigned end;
    char tag[4];
    char date[64];
    char month[64];
    static const char mon[12][4] =
            {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    struct tm t;

    GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
    fread(tag, 4, 1, GLOBAL_IO_ifp);
    size = read4bytes();
    end = ftell(GLOBAL_IO_ifp) + size;
    if ( !memcmp(tag, "RIFF", 4) || !memcmp(tag, "LIST", 4) ) {
        read4bytes();
        while ( ftell(GLOBAL_IO_ifp) + 7 < end && !feof(GLOBAL_IO_ifp) ) {
            parse_riff();
        }
    } else {
        if ( !memcmp(tag, "nctg", 4)) {
            while ( ftell(GLOBAL_IO_ifp) + 7 < end ) {
                i = read2bytes();
                size = read2bytes();
                if ((i + 1) >> 1 == 10 && size == 20 )
                    get_timestamp(0);
                else fseek(GLOBAL_IO_ifp, size, SEEK_CUR);
            }
        } else {
            if ( !memcmp(tag, "IDIT", 4) && size < 64 ) {
                fread(date, 64, 1, GLOBAL_IO_ifp);
                date[size] = 0;
                memset(&t, 0, sizeof t);
                if ( sscanf(date, "%*s %s %d %d:%d:%d %d", month, &t.tm_mday,
                            &t.tm_hour, &t.tm_min, &t.tm_sec, &t.tm_year) == 6 ) {
                    for ( i = 0; i < 12 && strcasecmp(mon[i], month); i++ );
                    t.tm_mon = i;
                    t.tm_year -= 1900;
                    if ( mktime(&t) > 0 ) {
                        timestamp = mktime(&t);
                    }
                }
            } else {
                fseek(GLOBAL_IO_ifp, size, SEEK_CUR);
            }
        }
    }
}

void
parse_crx(int end) {
    unsigned i;
    unsigned save;
    unsigned size;
    unsigned tag;
    unsigned base;
    static int index = 0;
    static int wide;
    static int high;
    static int off;
    static int len;

    GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
    while ( ftell(GLOBAL_IO_ifp) + 7 < end ) {
        save = ftell(GLOBAL_IO_ifp);
        if ((size = read4bytes()) < 8 ) {
            break;
        }
        switch ( tag = read4bytes() ) {
            case 0x6d6f6f76:
                // moov
            case 0x7472616b:
                // trak
            case 0x6d646961:
                // mdia
            case 0x6d696e66:
                // minf
            case 0x7374626c:
                // stbl
                parse_crx(save + size);
                break;
            case 0x75756964:
                // uuid
                switch ( i = read4bytes() ) {
                    case 0xeaf42b5e:
                        fseek(GLOBAL_IO_ifp, 8, SEEK_CUR);
                    case 0x85c0b687:
                        fseek(GLOBAL_IO_ifp, 12, SEEK_CUR);
                        parse_crx(save + size);
                }
                break;
            case 0x434d5431:
                // CMT1
            case 0x434d5432:
                // CMT2
                base = ftell(GLOBAL_IO_ifp);
                GLOBAL_endianOrder = read2bytes();
                fseek(GLOBAL_IO_ifp, 6, SEEK_CUR);
                if ( tag & 1 ) {
                    parse_tiff_ifd(base);
                } else {
                    parse_exif(base);
                }
                GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
                break;
            case 0x746b6864:
                // tkhd
                fseek(GLOBAL_IO_ifp, 12, SEEK_CUR);
                index = read4bytes();
                fseek(GLOBAL_IO_ifp, 58, SEEK_CUR);
                wide = read4bytes();
                high = read4bytes();
                break;
            case 0x7374737a:
                // stsz
                len = (read4bytes(), read4bytes());
                break;
            case 0x636f3634:
                // co64
                fseek(GLOBAL_IO_ifp, 12, SEEK_CUR);
                off = read4bytes();
                switch ( index ) {
                    case 1:
                        // 1 = full size, 2 = 27% size
                        thumb_width = wide;
                        thumb_height = high;
                        thumb_length = len;
                        thumb_offset = off;
                        break;
                    case 3:
                        THE_image.width = wide;
                        THE_image.height = high;
                        GLOBAL_IO_profileOffset = off;
                        TIFF_CALLBACK_loadRawData = &canon_crx_load_raw;
                        break;
                    default:
                        break;
                }
                break;
            case 0x50525657:
                // PRVW
                fseek(GLOBAL_IO_ifp, 6, SEEK_CUR);
                break;
            default:
                break;
        }
        fseek(GLOBAL_IO_ifp, save + size, SEEK_SET);
    }
}

void
parse_qt(int end) {
    unsigned save;
    unsigned size;
    char tag[4];

    GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
    while ( ftell(GLOBAL_IO_ifp) + 7 < end ) {
        save = ftell(GLOBAL_IO_ifp);
        if ((size = read4bytes()) < 8 ) {
            return;
        }
        fread(tag, 4, 1, GLOBAL_IO_ifp);
        if ( !memcmp(tag, "moov", 4) ||
             !memcmp(tag, "udta", 4) ||
             !memcmp(tag, "CNTH", 4) ) {
            parse_qt(save + size);
        }
        if ( !memcmp(tag, "CNDA", 4) ) {
            parse_jpeg(ftell(GLOBAL_IO_ifp));
        }
        fseek(GLOBAL_IO_ifp, save + size, SEEK_SET);
    }
}

void
parse_smal(int offset, int fsize) {
    int ver;

    fseek(GLOBAL_IO_ifp, offset + 2, SEEK_SET);
    GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
    ver = fgetc(GLOBAL_IO_ifp);
    if ( ver == 6 ) {
        fseek(GLOBAL_IO_ifp, 5, SEEK_CUR);
    }
    if ( read4bytes() != fsize ) {
        return;
    }
    if ( ver > 6 ) {
        GLOBAL_IO_profileOffset = read4bytes();
    }
    THE_image.height = height = read2bytes();
    THE_image.width = width = read2bytes();
    strcpy(GLOBAL_make, "SMaL");
    snprintf(GLOBAL_model, 64, "v%d %dx%d", ver, width, height);
    if ( ver == 6 ) {
        TIFF_CALLBACK_loadRawData = &smal_v6_load_raw;
    }
    if ( ver == 9 ) {
        TIFF_CALLBACK_loadRawData = &smal_v9_load_raw;
    }
}

void
parse_cine() {
    unsigned off_head;
    unsigned off_setup;
    unsigned off_image;
    unsigned i;

    GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
    fseek(GLOBAL_IO_ifp, 4, SEEK_SET);
    is_raw = read2bytes() == 2;
    fseek(GLOBAL_IO_ifp, 14, SEEK_CUR);
    is_raw *= read4bytes();
    off_head = read4bytes();
    off_setup = read4bytes();
    off_image = read4bytes();
    timestamp = read4bytes();
    if ( (i = read4bytes()) ) {
        timestamp = i;
    }
    fseek(GLOBAL_IO_ifp, off_head + 4, SEEK_SET);
    THE_image.width = read4bytes();
    THE_image.height = read4bytes();
    switch ( read2bytes(), read2bytes() ) {
        case 8:
            TIFF_CALLBACK_loadRawData = &eight_bit_load_raw;
            break;
        case 16:
            TIFF_CALLBACK_loadRawData = &unpacked_load_raw;
    }
    fseek(GLOBAL_IO_ifp, off_setup + 792, SEEK_SET);
    strcpy(GLOBAL_make, "CINE");
    snprintf(GLOBAL_model, 64, "%d", read4bytes());
    fseek(GLOBAL_IO_ifp, 12, SEEK_CUR);
    switch ((i = read4bytes()) & 0xffffff ) {
        case 3:
            IMAGE_filters = 0x94949494;
            break;
        case 4:
            IMAGE_filters = 0x49494949;
            break;
        default:
            is_raw = 0;
    }
    fseek(GLOBAL_IO_ifp, 72, SEEK_CUR);
    switch ((read4bytes() + 3600) % 360 ) {
        case 270:
            GLOBAL_flipsMask = 4;
            break;
        case 180:
            GLOBAL_flipsMask = 1;
            break;
        case 90:
            GLOBAL_flipsMask = 7;
            break;
        case 0:
            GLOBAL_flipsMask = 2;
    }
    GLOBAL_cam_mul[0] = readDouble(11);
    GLOBAL_cam_mul[2] = readDouble(11);
    ADOBE_maximum = ~(-1 << read4bytes());
    fseek(GLOBAL_IO_ifp, 668, SEEK_CUR);
    CAMERA_IMAGE_information.shutterSpeed = read4bytes() / 1000000000.0;
    fseek(GLOBAL_IO_ifp, off_image, SEEK_SET);
    if ( OPTIONS_values->shotSelect < is_raw ) {
        fseek(GLOBAL_IO_ifp, OPTIONS_values->shotSelect * 8, SEEK_CUR);
    }
    GLOBAL_IO_profileOffset = (INT64) read4bytes() + 8;
    GLOBAL_IO_profileOffset += (INT64) read4bytes() << 32;
}

void
parse_redcine() {
    unsigned i;
    unsigned len;
    unsigned rdvo;

    GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
    is_raw = 0;
    fseek(GLOBAL_IO_ifp, 52, SEEK_SET);
    width = read4bytes();
    height = read4bytes();
    fseek(GLOBAL_IO_ifp, 0, SEEK_END);
    fseek(GLOBAL_IO_ifp, -(i = ftello(GLOBAL_IO_ifp) & 511), SEEK_CUR);
    if ( read4bytes() != i || read4bytes() != 0x52454f42 ) {
        fprintf(stderr, _("%s: Tail is missing, parsing from head...\n"), CAMERA_IMAGE_information.inputFilename);
        fseek(GLOBAL_IO_ifp, 0, SEEK_SET);
        while ((len = read4bytes()) != EOF ) {
            if ( read4bytes() == 0x52454456 ) {
                if ( is_raw++ == OPTIONS_values->shotSelect ) {
                    GLOBAL_IO_profileOffset = ftello(GLOBAL_IO_ifp) - 8;
                }
            }
            fseek(GLOBAL_IO_ifp, len - 8, SEEK_CUR);
        }
    } else {
        rdvo = read4bytes();
        fseek(GLOBAL_IO_ifp, 12, SEEK_CUR);
        is_raw = read4bytes();
        fseeko(GLOBAL_IO_ifp, rdvo + 8 + OPTIONS_values->shotSelect * 4, SEEK_SET);
        GLOBAL_IO_profileOffset = read4bytes();
    }
}

char *
foveon_gets(int offset, char *str, int len) {
    int i;

    fseek(GLOBAL_IO_ifp, offset, SEEK_SET);
    for ( i = 0; i < len - 1; i++ ) {
        if ((str[i] = read2bytes()) == 0 ) {
            break;
        }
    }
    str[i] = 0;
    return str;
}

void
parse_foveon() {
    int entries;
    int img = 0;
    int off;
    int len;
    int tag;
    int save;
    int i;
    int wide;
    int high;
    int pent;
    int poff[256][2];
    char name[64];
    char value[64];

    GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
    fseek(GLOBAL_IO_ifp, 36, SEEK_SET);
    GLOBAL_flipsMask = read4bytes();
    fseek(GLOBAL_IO_ifp, -4, SEEK_END);
    fseek(GLOBAL_IO_ifp, read4bytes(), SEEK_SET);
    if ( read4bytes() != 0x64434553 ) {
        // SECd
        return;
    }
    entries = (read4bytes(), read4bytes());
    while ( entries-- ) {
        off = read4bytes();
        len = read4bytes();
        tag = read4bytes();
        save = ftell(GLOBAL_IO_ifp);
        fseek(GLOBAL_IO_ifp, off, SEEK_SET);
        if ( read4bytes() != (0x20434553 | (tag << 24))) {
            return;
        }
        switch ( tag ) {
            case 0x47414d49:
                // IMAG
            case 0x32414d49:
                // IMA2
                fseek(GLOBAL_IO_ifp, 8, SEEK_CUR);
                pent = read4bytes();
                wide = read4bytes();
                high = read4bytes();
                if ( wide > THE_image.width && high > THE_image.height ) {
                    switch ( pent ) {
                        case 5:
                            GLOBAL_loadFlags = 1;
                        case 6:
                            TIFF_CALLBACK_loadRawData = &foveon_sd_load_raw;
                            break;
                        case 30:
                            TIFF_CALLBACK_loadRawData = &foveon_dp_load_raw;
                            break;
                        default:
                            TIFF_CALLBACK_loadRawData = 0;
                    }
                    THE_image.width = wide;
                    THE_image.height = high;
                    GLOBAL_IO_profileOffset = off + 28;
                    is_foveon = 1;
                }
                fseek(GLOBAL_IO_ifp, off + 28, SEEK_SET);
                if ( fgetc(GLOBAL_IO_ifp) == 0xff && fgetc(GLOBAL_IO_ifp) == 0xd8
                     && thumb_length < len - 28 ) {
                    thumb_offset = off + 28;
                    thumb_length = len - 28;
                    write_thumb = &jpeg_thumb;
                }
                if ( ++img == 2 && !thumb_length ) {
                    thumb_offset = off + 24;
                    thumb_width = wide;
                    thumb_height = high;
                    write_thumb = &foveon_thumb;
                }
                break;
            case 0x464d4143:
                // CAMF
                GLOBAL_meta_offset = off + 8;
                meta_length = len - 28;
                break;
            case 0x504f5250:
                // PROP
                pent = (read4bytes(), read4bytes());
                fseek(GLOBAL_IO_ifp, 12, SEEK_CUR);
                off += pent * 8 + 24;
                if ( (unsigned) pent > 256 ) {
                    pent = 256;
                }
                for ( i = 0; i < pent * 2; i++ ) {
                    ((int *) poff)[i] = off + read4bytes() * 2;
                }
                for ( i = 0; i < pent; i++ ) {
                    foveon_gets(poff[i][0], name, 64);
                    foveon_gets(poff[i][1], value, 64);
                    if ( !strcmp(name, "ISO") ) {
                        iso_speed = atoi(value);
                    }
                    if ( !strcmp(name, "CAMMANUF") ) {
                        strcpy(GLOBAL_make, value);
                    }
                    if ( !strcmp(name, "CAMMODEL") ) {
                        strcpy(GLOBAL_model, value);
                    }
                    if ( !strcmp(name, "WB_DESC") ) {
                        strcpy(model2, value);
                    }
                    if ( !strcmp(name, "TIME") ) {
                        timestamp = atoi(value);
                    }
                    if ( !strcmp(name, "EXPTIME") ) {
                        CAMERA_IMAGE_information.shutterSpeed = atoi(value) / 1000000.0;
                    }
                    if ( !strcmp(name, "APERTURE") ) {
                        aperture = atof(value);
                    }
                    if ( !strcmp(name, "FLENGTH") ) {
                        focal_len = atof(value);
                    }
                }
#ifdef LOCALTIME
                timestamp = mktime(gmtime(&timestamp));
#endif
                break;
            default:
                break;
        }
        fseek(GLOBAL_IO_ifp, save, SEEK_SET);
    }
}

/*
All matrices are from Adobe DNG Converter unless otherwise noted.
*/
void
adobe_coeff(const char *adobeMake, const char *adobeModel) {
    static const struct {
        const char *prefix;
        short black;
        short maximum;
        short trans[12];
    } table[] = {
            {"AgfaPhoto DC-833m",             0,   0,    /* DJC */
                    {11438, -3762,  -1115, -2409, 9914,  2497,  -1227, 2295,  5300}},
            {"Apple QuickTake",               0,   0,        /* DJC */
                    {21392, -5653,  -3353, 2406,  8010,  -415,  7166,  1427,  2078}},
            {"Canon EOS D2000",               0,   0,
                    {24542, -10860, -3401, -1490, 11370, -297,  2858,  -605,  3225}},
            {"Canon EOS D6000",               0,   0,
                    {20482, -7172,  -3125, -1033, 10410, -285,  2542,  226,   3136}},
            {"Canon EOS D30",                 0,   0,
                    {9805,  -2689,  -1312, -5803, 13064, 3068,  -2438, 3075,  8775}},
            {"Canon EOS D60",                 0,   0xfa0,
                    {6188,  -1341,  -890,  -7168, 14489, 2937,  -2640, 3228,  8483}},
            {"Canon EOS 5DS",                 0,   0x3c96,
                    {6250,  -711,   -808,  -5153, 12794, 2636,  -1249, 2198,  5610}},
            {"Canon EOS 5D Mark IV",          0,   0,
                    {6446,  -366,   -864,  -4436, 12204, 2513,  -952,  2496,  6348}},
            {"Canon EOS 5D Mark III",         0,   0x3c80,
                    {6722,  -635,   -963,  -4287, 12460, 2028,  -908,  2162,  5668}},
            {"Canon EOS 5D Mark II",          0,   0x3cf0,
                    {4716,  603,    -830,  -7798, 15474, 2480,  -1496, 1937,  6651}},
            {"Canon EOS 5D",                  0,   0xe6c,
                    {6347,  -479,   -972,  -8297, 15954, 2480,  -1968, 2131,  7649}},
            {"Canon EOS 6D Mark II",          0,   0,
                    {6875,  -970,   -932,  -4691, 12459, 2501,  -874,  1953,  5809}},
            {"Canon EOS 6D",                  0,   0x3c82,
                    {7034,  -804,   -1014, -4420, 12564, 2058,  -851,  1994,  5758}},
            {"Canon EOS 7D Mark II",          0,   0x3510,
                    {7268,  -1082,  -969,  -4186, 11839, 2663,  -825,  2029,  5839}},
            {"Canon EOS 7D",                  0,   0x3510,
                    {6844,  -996,   -856,  -3876, 11761, 2396,  -593,  1772,  6198}},
            {"Canon EOS 10D",                 0,   0xfa0,
                    {8197,  -2000,  -1118, -6714, 14335, 2592,  -2536, 3178,  8266}},
            {"Canon EOS 20Da",                0,   0,
                    {14155, -5065,  -1382, -6550, 14633, 2039,  -1623, 1824,  6561}},
            {"Canon EOS 20D",                 0,   0xfff,
                    {6599,  -537,   -891,  -8071, 15783, 2424,  -1983, 2234,  7462}},
            {"Canon EOS 30D",                 0,   0,
                    {6257,  -303,   -1000, -7880, 15621, 2396,  -1714, 1904,  7046}},
            {"Canon EOS 40D",                 0,   0x3f60,
                    {6071,  -747,   -856,  -7653, 15365, 2441,  -2025, 2553,  7315}},
            {"Canon EOS 50D",                 0,   0x3d93,
                    {4920,  616,    -593,  -6493, 13964, 2784,  -1774, 3178,  7005}},
            {"Canon EOS 60D",                 0,   0x2ff7,
                    {6719,  -994,   -925,  -4408, 12426, 2211,  -887,  2129,  6051}},
            {"Canon EOS 70D",                 0,   0x3bc7,
                    {7034,  -804,   -1014, -4420, 12564, 2058,  -851,  1994,  5758}},
            {"Canon EOS 77D",                 0,   0,
                    {7377,  -742,   -998,  -4235, 11981, 2549,  -673,  1918,  5538}},
            {"Canon EOS 80D",                 0,   0,
                    {7457,  -671,   -937,  -4849, 12495, 2643,  -1213, 2354,  5492}},
            {"Canon EOS 100D",                0,   0x350f,
                    {6602,  -841,   -939,  -4472, 12458, 2247,  -975,  2039,  6148}},
            {"Canon EOS 200D",                0,   0,
                    {7377,  -742,   -998,  -4235, 11981, 2549,  -673,  1918,  5538}},
            {"Canon EOS 300D",                0,   0xfa0,
                    {8197,  -2000,  -1118, -6714, 14335, 2592,  -2536, 3178,  8266}},
            {"Canon EOS 350D",                0,   0xfff,
                    {6018,  -617,   -965,  -8645, 15881, 2975,  -1530, 1719,  7642}},
            {"Canon EOS 400D",                0,   0xe8e,
                    {7054,  -1501,  -990,  -8156, 15544, 2812,  -1278, 1414,  7796}},
            {"Canon EOS 450D",                0,   0x390d,
                    {5784,  -262,   -821,  -7539, 15064, 2672,  -1982, 2681,  7427}},
            {"Canon EOS 500D",                0,   0x3479,
                    {4763,  712,    -646,  -6821, 14399, 2640,  -1921, 3276,  6561}},
            {"Canon EOS 550D",                0,   0x3dd7,
                    {6941,  -1164,  -857,  -3825, 11597, 2534,  -416,  1540,  6039}},
            {"Canon EOS 600D",                0,   0x3510,
                    {6461,  -907,   -882,  -4300, 12184, 2378,  -819,  1944,  5931}},
            {"Canon EOS 650D",                0,   0x354d,
                    {6602,  -841,   -939,  -4472, 12458, 2247,  -975,  2039,  6148}},
            {"Canon EOS 700D",                0,   0x3c00,
                    {6602,  -841,   -939,  -4472, 12458, 2247,  -975,  2039,  6148}},
            {"Canon EOS 750D",                0,   0x368e,
                    {6362,  -823,   -847,  -4426, 12109, 2616,  -743,  1857,  5635}},
            {"Canon EOS 760D",                0,   0x350f,
                    {6362,  -823,   -847,  -4426, 12109, 2616,  -743,  1857,  5635}},
            {"Canon EOS 800D",                0,   0,
                    {6970,  -512,   -968,  -4425, 12161, 2553,  -739,  1982,  5601}},
            {"Canon EOS 1000D",               0,   0xe43,
                    {6771,  -1139,  -977,  -7818, 15123, 2928,  -1244, 1437,  7533}},
            {"Canon EOS 1100D",               0,   0x3510,
                    {6444,  -904,   -893,  -4563, 12308, 2535,  -903,  2016,  6728}},
            {"Canon EOS 1200D",               0,   0x37c2,
                    {6461,  -907,   -882,  -4300, 12184, 2378,  -819,  1944,  5931}},
            {"Canon EOS 1300D",               0,   0x3510,
                    {6939,  -1016,  -866,  -4428, 12473, 2177,  -1175, 2178,  6162}},
            {"Canon EOS 1500D",               0,   0,
                    {8532,  -701,   -1167, -4095, 11879, 2508,  -797,  2424,  7010}},
            {"Canon EOS 3000D",               0,   0,
                    {6939,  -1016,  -866,  -4428, 12473, 2177,  -1175, 2178,  6162}},
            {"Canon EOS M6",                  0,   0,
                    {8532,  -701,   -1167, -4095, 11879, 2508,  -797,  2424,  7010}},
            {"Canon EOS M5",                  0,   0,    /* also M50 */
                    {8532,  -701,   -1167, -4095, 11879, 2508,  -797,  2424,  7010}},
            {"Canon EOS M3",                  0,   0,
                    {6362,  -823,   -847,  -4426, 12109, 2616,  -743,  1857,  5635}},
            {"Canon EOS M100",                0,   0,
                    {8532,  -701,   -1167, -4095, 11879, 2508,  -797,  2424,  7010}},
            {"Canon EOS M10",                 0,   0,
                    {6400,  -480,   -888,  -5294, 13416, 2047,  -1296, 2203,  6137}},
            {"Canon EOS M",                   0,   0,
                    {6602,  -841,   -939,  -4472, 12458, 2247,  -975,  2039,  6148}},
            {"Canon EOS-1Ds Mark III",        0,   0x3bb0,
                    {5859,  -211,   -930,  -8255, 16017, 2353,  -1732, 1887,  7448}},
            {"Canon EOS-1Ds Mark II",         0,   0xe80,
                    {6517,  -602,   -867,  -8180, 15926, 2378,  -1618, 1771,  7633}},
            {"Canon EOS-1D Mark IV",          0,   0x3bb0,
                    {6014,  -220,   -795,  -4109, 12014, 2361,  -561,  1824,  5787}},
            {"Canon EOS-1D Mark III",         0,   0x3bb0,
                    {6291,  -540,   -976,  -8350, 16145, 2311,  -1714, 1858,  7326}},
            {"Canon EOS-1D Mark II N",        0,   0xe80,
                    {6240,  -466,   -822,  -8180, 15825, 2500,  -1801, 1938,  8042}},
            {"Canon EOS-1D Mark II",          0,   0xe80,
                    {6264,  -582,   -724,  -8312, 15948, 2504,  -1744, 1919,  8664}},
            {"Canon EOS-1DS",                 0,   0xe20,
                    {4374,  3631,   -1743, -7520, 15212, 2472,  -2892, 3632,  8161}},
            {"Canon EOS-1D C",                0,   0x3c4e,
                    {6847,  -614,   -1014, -4669, 12737, 2139,  -1197, 2488,  6846}},
            {"Canon EOS-1D X Mark II",        0,   0,
                    {7596,  -978,   -967,  -4808, 12571, 2503,  -1398, 2567,  5752}},
            {"Canon EOS-1D X",                0,   0x3c4e,
                    {6847,  -614,   -1014, -4669, 12737, 2139,  -1197, 2488,  6846}},
            {"Canon EOS-1D",                  0,   0xe20,
                    {6806,  -179,   -1020, -8097, 16415, 1687,  -3267, 4236,  7690}},
            {"Canon EOS C500",                853, 0,        /* DJC */
                    {17851, -10604, 922,   -7425, 16662, 763,   -3660, 3636,  22278}},
            {"Canon PowerShot A530",          0,   0,
                    {0}},    /* don't want the A5 matrix */
            {"Canon PowerShot A50",           0,   0,
                    {-5300, 9846,   1776,  3436,  684,   3939,  -5540, 9879,  6200, -1404, 11175, 217}},
            {"Canon PowerShot A5",            0,   0,
                    {-4801, 9475,   1952,  2926,  1611,  4094,  -5259, 10164, 5947, -1554, 10883, 547}},
            {"Canon PowerShot G10",           0,   0,
                    {11093, -3906,  -1028, -5047, 12492, 2879,  -1003, 1750,  5561}},
            {"Canon PowerShot G11",           0,   0,
                    {12177, -4817,  -1069, -1612, 9864,  2049,  -98,   850,   4471}},
            {"Canon PowerShot G12",           0,   0,
                    {13244, -5501,  -1248, -1508, 9858,  1935,  -270,  1083,  4366}},
            {"Canon PowerShot G15",           0,   0,
                    {7474,  -2301,  -567,  -4056, 11456, 2975,  -222,  716,   4181}},
            {"Canon PowerShot G16",           0,   0,
                    {8020,  -2687,  -682,  -3704, 11879, 2052,  -965,  1921,  5556}},
            {"Canon PowerShot G1 X Mark III", 0,   0,
                    {8532,  -701,   -1167, -4095, 11879, 2508,  -797,  2424,  7010}},
            {"Canon PowerShot G1 X",          0,   0,
                    {7378,  -1255,  -1043, -4088, 12251, 2048,  -876,  1946,  5805}},
            {"Canon PowerShot G1",            0,   0,
                    {-4778, 9467,   2172,  4743,  -1141, 4344,  -5146, 9908,  6077, -1566, 11051, 557}},
            {"Canon PowerShot G2",            0,   0,
                    {9087,  -2693,  -1049, -6715, 14382, 2537,  -2291, 2819,  7790}},
            {"Canon PowerShot G3 X",          0,   0,
                    {9701,  -3857,  -921,  -3149, 11537, 1817,  -786,  1817,  5147}},
            {"Canon PowerShot G3",            0,   0,
                    {9212,  -2781,  -1073, -6573, 14189, 2605,  -2300, 2844,  7664}},
            {"Canon PowerShot G5 X",          0,   0,
                    {9602,  -3823,  -937,  -2984, 11495, 1675,  -407,  1415,  5049}},
            {"Canon PowerShot G5",            0,   0,
                    {9757,  -2872,  -933,  -5972, 13861, 2301,  -1622, 2328,  7212}},
            {"Canon PowerShot G6",            0,   0,
                    {9877,  -3775,  -871,  -7613, 14807, 3072,  -1448, 1305,  7485}},
            {"Canon PowerShot G7 X",          0,   0,
                    {9602,  -3823,  -937,  -2984, 11495, 1675,  -407,  1415,  5049}},
            {"Canon PowerShot G9 X Mark II",  0,   0,
                    {10056, -4131,  -944,  -2576, 11143, 1625,  -238,  1294,  5179}},
            {"Canon PowerShot G9 X",          0,   0,
                    {9602,  -3823,  -937,  -2984, 11495, 1675,  -407,  1415,  5049}},
            {"Canon PowerShot G9",            0,   0,
                    {7368,  -2141,  -598,  -5621, 13254, 2625,  -1418, 1696,  5743}},
            {"Canon PowerShot Pro1",          0,   0,
                    {10062, -3522,  -999,  -7643, 15117, 2730,  -765,  817,   7323}},
            {"Canon PowerShot Pro70",         34,  0,
                    {-4155, 9818,   1529,  3939,  -25,   4522,  -5521, 9870,  6610, -2238, 10873, 1342}},
            {"Canon PowerShot Pro90",         0,   0,
                    {-4963, 9896,   2235,  4642,  -987,  4294,  -5162, 10011, 5859, -1770, 11230, 577}},
            {"Canon PowerShot S30",           0,   0,
                    {10566, -3652,  -1129, -6552, 14662, 2006,  -2197, 2581,  7670}},
            {"Canon PowerShot S40",           0,   0,
                    {8510,  -2487,  -940,  -6869, 14231, 2900,  -2318, 2829,  9013}},
            {"Canon PowerShot S45",           0,   0,
                    {8163,  -2333,  -955,  -6682, 14174, 2751,  -2077, 2597,  8041}},
            {"Canon PowerShot S50",           0,   0,
                    {8882,  -2571,  -863,  -6348, 14234, 2288,  -1516, 2172,  6569}},
            {"Canon PowerShot S60",           0,   0,
                    {8795,  -2482,  -797,  -7804, 15403, 2573,  -1422, 1996,  7082}},
            {"Canon PowerShot S70",           0,   0,
                    {9976,  -3810,  -832,  -7115, 14463, 2906,  -901,  989,   7889}},
            {"Canon PowerShot S90",           0,   0,
                    {12374, -5016,  -1049, -1677, 9902,  2078,  -83,   852,   4683}},
            {"Canon PowerShot S95",           0,   0,
                    {13440, -5896,  -1279, -1236, 9598,  1931,  -180,  1001,  4651}},
            {"Canon PowerShot S100",          0,   0,
                    {7968,  -2565,  -636,  -2873, 10697, 2513,  180,   667,   4211}},
            {"Canon PowerShot S110",          0,   0,
                    {8039,  -2643,  -654,  -3783, 11230, 2930,  -206,  690,   4194}},
            {"Canon PowerShot S120",          0,   0,
                    {6961,  -1685,  -695,  -4625, 12945, 1836,  -1114, 2152,  5518}},
            {"Canon PowerShot SX1 IS",        0,   0,
                    {6578,  -259,   -502,  -5974, 13030, 3309,  -308,  1058,  4970}},
            {"Canon PowerShot SX50 HS",       0,   0,
                    {12432, -4753,  -1247, -2110, 10691, 1629,  -412,  1623,  4926}},
            {"Canon PowerShot SX60 HS",       0,   0,
                    {13161, -5451,  -1344, -1989, 10654, 1531,  -47,   1271,  4955}},
            {"Canon PowerShot A3300",         0,   0,    /* DJC */
                    {10826, -3654,  -1023, -3215, 11310, 1906,  0,     999,   4960}},
            {"Canon PowerShot A470",          0,   0,    /* DJC */
                    {12513, -4407,  -1242, -2680, 10276, 2405,  -878,  2215,  4734}},
            {"Canon PowerShot A610",          0,   0,    /* DJC */
                    {15591, -6402,  -1592, -5365, 13198, 2168,  -1300, 1824,  5075}},
            {"Canon PowerShot A620",          0,   0,    /* DJC */
                    {15265, -6193,  -1558, -4125, 12116, 2010,  -888,  1639,  5220}},
            {"Canon PowerShot A630",          0,   0,    /* DJC */
                    {14201, -5308,  -1757, -6087, 14472, 1617,  -2191, 3105,  5348}},
            {"Canon PowerShot A640",          0,   0,    /* DJC */
                    {13124, -5329,  -1390, -3602, 11658, 1944,  -1612, 2863,  4885}},
            {"Canon PowerShot A650",          0,   0,    /* DJC */
                    {9427,  -3036,  -959,  -2581, 10671, 1911,  -1039, 1982,  4430}},
            {"Canon PowerShot A720",          0,   0,    /* DJC */
                    {14573, -5482,  -1546, -1266, 9799,  1468,  -1040, 1912,  3810}},
            {"Canon PowerShot S3 IS",         0,   0,    /* DJC */
                    {14062, -5199,  -1446, -4712, 12470, 2243,  -1286, 2028,  4836}},
            {"Canon PowerShot SX110 IS",      0,   0,    /* DJC */
                    {14134, -5576,  -1527, -1991, 10719, 1273,  -1158, 1929,  3581}},
            {"Canon PowerShot SX220",         0,   0,    /* DJC */
                    {13898, -5076,  -1447, -1405, 10109, 1297,  -244,  1860,  3687}},
            {"Canon IXUS 160",                0,   0,        /* DJC */
                    {11657, -3781,  -1136, -3544, 11262, 2283,  -160,  1219,  4700}},
            {"Casio EX-S20",                  0,   0,        /* DJC */
                    {11634, -3924,  -1128, -4968, 12954, 2015,  -1588, 2648,  7206}},
            {"Casio EX-Z750",                 0,   0,        /* DJC */
                    {10819, -3873,  -1099, -4903, 13730, 1175,  -1755, 3751,  4632}},
            {"Casio EX-Z10",                  128, 0xfff,    /* DJC */
                    {9790,  -3338,  -603,  -2321, 10222, 2099,  -344,  1273,  4799}},
            {"CINE 650",                      0,   0,
                    {3390,  480,    -500,  -800,  3610,  340,   -550,  2336,  1192}},
            {"CINE 660",                      0,   0,
                    {3390,  480,    -500,  -800,  3610,  340,   -550,  2336,  1192}},
            {"CINE",                          0,   0,
                    {20183, -4295,  -423,  -3940, 15330, 3985,  -280,  4870,  9800}},
            {"Contax N Digital",              0,   0xf1e,
                    {7777,  1285,   -1053, -9280, 16543, 2916,  -3677, 5679,  7060}},
            {"DXO ONE",                       0,   0,
                    {6596,  -2079,  -562,  -4782, 13016, 1933,  -970,  1581,  5181}},
            {"Epson R-D1",                    0,   0,
                    {6827,  -1878,  -732,  -8429, 16012, 2564,  -704,  592,   7145}},
            {"Fujifilm E550",                 0,   0,
                    {11044, -3888,  -1120, -7248, 15168, 2208,  -1531, 2277,  8069}},
            {"Fujifilm E900",                 0,   0,
                    {9183,  -2526,  -1078, -7461, 15071, 2574,  -2022, 2440,  8639}},
            {"Fujifilm F5",                   0,   0,
                    {13690, -5358,  -1474, -3369, 11600, 1998,  -132,  1554,  4395}},
            {"Fujifilm F6",                   0,   0,
                    {13690, -5358,  -1474, -3369, 11600, 1998,  -132,  1554,  4395}},
            {"Fujifilm F77",                  0,   0xfe9,
                    {13690, -5358,  -1474, -3369, 11600, 1998,  -132,  1554,  4395}},
            {"Fujifilm F7",                   0,   0,
                    {10004, -3219,  -1201, -7036, 15047, 2107,  -1863, 2565,  7736}},
            {"Fujifilm F8",                   0,   0,
                    {13690, -5358,  -1474, -3369, 11600, 1998,  -132,  1554,  4395}},
            {"Fujifilm GFX 50S",              0,   0,
                    {11756, -4754,  -874,  -3056, 11045, 2305,  -381,  1457,  6006}},
            {"Fujifilm S100FS",               514, 0,
                    {11521, -4355,  -1065, -6524, 13767, 3058,  -1466, 1984,  6045}},
            {"Fujifilm S1",                   0,   0,
                    {12297, -4882,  -1202, -2106, 10691, 1623,  -88,   1312,  4790}},
            {"Fujifilm S20Pro",               0,   0,
                    {10004, -3219,  -1201, -7036, 15047, 2107,  -1863, 2565,  7736}},
            {"Fujifilm S20",                  512, 0x3fff,
                    {11401, -4498,  -1312, -5088, 12751, 2613,  -838,  1568,  5941}},
            {"Fujifilm S2Pro",                128, 0xf15,
                    {12492, -4690,  -1402, -7033, 15423, 1647,  -1507, 2111,  7697}},
            {"Fujifilm S3Pro",                0,   0x3dff,
                    {11807, -4612,  -1294, -8927, 16968, 1988,  -2120, 2741,  8006}},
            {"Fujifilm S5Pro",                0,   0,
                    {12300, -5110,  -1304, -9117, 17143, 1998,  -1947, 2448,  8100}},
            {"Fujifilm S5000",                0,   0,
                    {8754,  -2732,  -1019, -7204, 15069, 2276,  -1702, 2334,  6982}},
            {"Fujifilm S5100",                0,   0,
                    {11940, -4431,  -1255, -6766, 14428, 2542,  -993,  1165,  7421}},
            {"Fujifilm S5500",                0,   0,
                    {11940, -4431,  -1255, -6766, 14428, 2542,  -993,  1165,  7421}},
            {"Fujifilm S5200",                0,   0,
                    {9636,  -2804,  -988,  -7442, 15040, 2589,  -1803, 2311,  8621}},
            {"Fujifilm S5600",                0,   0,
                    {9636,  -2804,  -988,  -7442, 15040, 2589,  -1803, 2311,  8621}},
            {"Fujifilm S6",                   0,   0,
                    {12628, -4887,  -1401, -6861, 14996, 1962,  -2198, 2782,  7091}},
            {"Fujifilm S7000",                0,   0,
                    {10190, -3506,  -1312, -7153, 15051, 2238,  -2003, 2399,  7505}},
            {"Fujifilm S9000",                0,   0,
                    {10491, -3423,  -1145, -7385, 15027, 2538,  -1809, 2275,  8692}},
            {"Fujifilm S9500",                0,   0,
                    {10491, -3423,  -1145, -7385, 15027, 2538,  -1809, 2275,  8692}},
            {"Fujifilm S9100",                0,   0,
                    {12343, -4515,  -1285, -7165, 14899, 2435,  -1895, 2496,  8800}},
            {"Fujifilm S9600",                0,   0,
                    {12343, -4515,  -1285, -7165, 14899, 2435,  -1895, 2496,  8800}},
            {"Fujifilm SL1000",               0,   0,
                    {11705, -4262,  -1107, -2282, 10791, 1709,  -555,  1713,  4945}},
            {"Fujifilm IS-1",                 0,   0,
                    {21461, -10807, -1441, -2332, 10599, 1999,  289,   875,   7703}},
            {"Fujifilm IS Pro",               0,   0,
                    {12300, -5110,  -1304, -9117, 17143, 1998,  -1947, 2448,  8100}},
            {"Fujifilm HS10 HS11",            0,   0xf68,
                    {12440, -3954,  -1183, -1123, 9674,  1708,  -83,   1614,  4086}},
            {"Fujifilm HS2",                  0,   0xfef,
                    {13690, -5358,  -1474, -3369, 11600, 1998,  -132,  1554,  4395}},
            {"Fujifilm HS3",                  0,   0,
                    {13690, -5358,  -1474, -3369, 11600, 1998,  -132,  1554,  4395}},
            {"Fujifilm HS50EXR",              0,   0,
                    {12085, -4727,  -953,  -3257, 11489, 2002,  -511,  2046,  4592}},
            {"Fujifilm F900EXR",              0,   0,
                    {12085, -4727,  -953,  -3257, 11489, 2002,  -511,  2046,  4592}},
            {"Fujifilm X100F",                0,   0,
                    {11434, -4948,  -1210, -3746, 12042, 1903,  -666,  1479,  5235}},
            {"Fujifilm X100S",                0,   0,
                    {10592, -4262,  -1008, -3514, 11355, 2465,  -870,  2025,  6386}},
            {"Fujifilm X100T",                0,   0,
                    {10592, -4262,  -1008, -3514, 11355, 2465,  -870,  2025,  6386}},
            {"Fujifilm X100",                 0,   0,
                    {12161, -4457,  -1069, -5034, 12874, 2400,  -795,  1724,  6904}},
            {"Fujifilm X10",                  0,   0,
                    {13509, -6199,  -1254, -4430, 12733, 1865,  -331,  1441,  5022}},
            {"Fujifilm X20",                  0,   0,
                    {11768, -4971,  -1133, -4904, 12927, 2183,  -480,  1723,  4605}},
            {"Fujifilm X30",                  0,   0,
                    {12328, -5256,  -1144, -4469, 12927, 1675,  -87,   1291,  4351}},
            {"Fujifilm X70",                  0,   0,
                    {10450, -4329,  -878,  -3217, 11105, 2421,  -752,  1758,  6519}},
            {"Fujifilm X-Pro1",               0,   0,
                    {10413, -3996,  -993,  -3721, 11640, 2361,  -733,  1540,  6011}},
            {"Fujifilm X-Pro2",               0,   0,
                    {11434, -4948,  -1210, -3746, 12042, 1903,  -666,  1479,  5235}},
            {"Fujifilm X-A10",                0,   0,
                    {11540, -4999,  -991,  -2949, 10963, 2278,  -382,  1049,  5605}},
            {"Fujifilm X-A20",                0,   0,
                    {11540, -4999,  -991,  -2949, 10963, 2278,  -382,  1049,  5605}},
            {"Fujifilm X-A1",                 0,   0,
                    {11086, -4555,  -839,  -3512, 11310, 2517,  -815,  1341,  5940}},
            {"Fujifilm X-A2",                 0,   0,
                    {10763, -4560,  -917,  -3346, 11311, 2322,  -475,  1135,  5843}},
            {"Fujifilm X-A3",                 0,   0,
                    {12407, -5222,  -1086, -2971, 11116, 2120,  -294,  1029,  5284}},
            {"Fujifilm X-A5",                 0,   0,
                    {11673, -4760,  -1041, -3988, 12058, 2166,  -771,  1417,  5569}},
            {"Fujifilm X-E1",                 0,   0,
                    {10413, -3996,  -993,  -3721, 11640, 2361,  -733,  1540,  6011}},
            {"Fujifilm X-E2S",                0,   0,
                    {11562, -5118,  -961,  -3022, 11007, 2311,  -525,  1569,  6097}},
            {"Fujifilm X-E2",                 0,   0,
                    {8458,  -2451,  -855,  -4597, 12447, 2407,  -1475, 2482,  6526}},
            {"Fujifilm X-E3",                 0,   0,
                    {11434, -4948,  -1210, -3746, 12042, 1903,  -666,  1479,  5235}},
            {"Fujifilm X-H1",                 0,   0,
                    {11434, -4948,  -1210, -3746, 12042, 1903,  -666,  1479,  5235}},
            {"Fujifilm X-M1",                 0,   0,
                    {10413, -3996,  -993,  -3721, 11640, 2361,  -733,  1540,  6011}},
            {"Fujifilm X-S1",                 0,   0,
                    {13509, -6199,  -1254, -4430, 12733, 1865,  -331,  1441,  5022}},
            {"Fujifilm X-T1",                 0,   0,    /* also X-T10 */
                    {8458,  -2451,  -855,  -4597, 12447, 2407,  -1475, 2482,  6526}},
            {"Fujifilm X-T2",                 0,   0,    /* also X-T20 */
                    {11434, -4948,  -1210, -3746, 12042, 1903,  -666,  1479,  5235}},
            {"Fujifilm XF1",                  0,   0,
                    {13509, -6199,  -1254, -4430, 12733, 1865,  -331,  1441,  5022}},
            {"Fujifilm XQ",                   0,   0,    /* XQ1 and XQ2 */
                    {9252,  -2704,  -1064, -5893, 14265, 1717,  -1101, 2341,  4349}},
            {"GoPro HERO5 Black",             0,   0,
                    {10344, -4210,  -620,  -2315, 10625, 1948,  93,    1058,  5541}},
            {"Imacon Ixpress",                0,   0,        /* DJC */
                    {7025,  -1415,  -704,  -5188, 13765, 1424,  -1248, 2742,  6038}},
            {"Kodak NC2000",                  0,   0,
                    {13891, -6055,  -803,  -465,  9919,  642,   2121,  82,    1291}},
            {"Kodak DCS315C",                 8,   0,
                    {17523, -4827,  -2510, 756,   8546,  -137,  6113,  1649,  2250}},
            {"Kodak DCS330C",                 8,   0,
                    {20620, -7572,  -2801, -103,  10073, -396,  3551,  -233,  2220}},
            {"Kodak DCS420",                  0,   0,
                    {10868, -1852,  -644,  -1537, 11083, 484,   2343,  628,   2216}},
            {"Kodak DCS460",                  0,   0,
                    {10592, -2206,  -967,  -1944, 11685, 230,   2206,  670,   1273}},
            {"Kodak EOSDCS1",                 0,   0,
                    {10592, -2206,  -967,  -1944, 11685, 230,   2206,  670,   1273}},
            {"Kodak EOSDCS3B",                0,   0,
                    {9898,  -2700,  -940,  -2478, 12219, 206,   1985,  634,   1031}},
            {"Kodak DCS520C",                 178, 0,
                    {24542, -10860, -3401, -1490, 11370, -297,  2858,  -605,  3225}},
            {"Kodak DCS560C",                 177, 0,
                    {20482, -7172,  -3125, -1033, 10410, -285,  2542,  226,   3136}},
            {"Kodak DCS620C",                 177, 0,
                    {23617, -10175, -3149, -2054, 11749, -272,  2586,  -489,  3453}},
            {"Kodak DCS620X",                 176, 0,
                    {13095, -6231,  154,   12221, -21,   -2137, 895,   4602,  2258}},
            {"Kodak DCS660C",                 173, 0,
                    {18244, -6351,  -2739, -791,  11193, -521,  3711,  -129,  2802}},
            {"Kodak DCS720X",                 0,   0,
                    {11775, -5884,  950,   9556,  1846,  -1286, -1019, 6221,  2728}},
            {"Kodak DCS760C",                 0,   0,
                    {16623, -6309,  -1411, -4344, 13923, 323,   2285,  274,   2926}},
            {"Kodak DCS Pro SLR",             0,   0,
                    {5494,  2393,   -232,  -6427, 13850, 2846,  -1876, 3997,  5445}},
            {"Kodak DCS Pro 14nx",            0,   0,
                    {5494,  2393,   -232,  -6427, 13850, 2846,  -1876, 3997,  5445}},
            {"Kodak DCS Pro 14",              0,   0,
                    {7791,  3128,   -776,  -8588, 16458, 2039,  -2455, 4006,  6198}},
            {"Kodak ProBack645",              0,   0,
                    {16414, -6060,  -1470, -3555, 13037, 473,   2545,  122,   4948}},
            {"Kodak ProBack",                 0,   0,
                    {21179, -8316,  -2918, -915,  11019, -165,  3477,  -180,  4210}},
            {"Kodak P712",                    0,   0,
                    {9658,  -3314,  -823,  -5163, 12695, 2768,  -1342, 1843,  6044}},
            {"Kodak P850",                    0,   0xf7c,
                    {10511, -3836,  -1102, -6946, 14587, 2558,  -1481, 1792,  6246}},
            {"Kodak P880",                    0,   0xfff,
                    {12805, -4662,  -1376, -7480, 15267, 2360,  -1626, 2194,  7904}},
            {"Kodak EasyShare Z980",          0,   0,
                    {11313, -3559,  -1101, -3893, 11891, 2257,  -1214, 2398,  4908}},
            {"Kodak EasyShare Z981",          0,   0,
                    {12729, -4717,  -1188, -1367, 9187,  2582,  274,   860,   4411}},
            {"Kodak EasyShare Z990",          0,   0xfed,
                    {11749, -4048,  -1309, -1867, 10572, 1489,  -138,  1449,  4522}},
            {"Kodak EASYSHARE Z1015",         0,   0xef1,
                    {11265, -4286,  -992,  -4694, 12343, 2647,  -1090, 1523,  5447}},
            {"Leaf CMost",                    0,   0,
                    {3952,  2189,   449,   -6701, 14585, 2275,  -4536, 7349,  6536}},
            {"Leaf Valeo 6",                  0,   0,
                    {3952,  2189,   449,   -6701, 14585, 2275,  -4536, 7349,  6536}},
            {"Leaf Aptus 54S",                0,   0,
                    {8236,  1746,   -1314, -8251, 15953, 2428,  -3673, 5786,  5771}},
            {"Leaf Aptus 65",                 0,   0,
                    {7914,  1414,   -1190, -8777, 16582, 2280,  -2811, 4605,  5562}},
            {"Leaf Aptus 75",                 0,   0,
                    {7914,  1414,   -1190, -8777, 16582, 2280,  -2811, 4605,  5562}},
            {"Leaf",                          0,   0,
                    {8236,  1746,   -1314, -8251, 15953, 2428,  -3673, 5786,  5771}},
            {"Mamiya ZD",                     0,   0,
                    {7645,  2579,   -1363, -8689, 16717, 2015,  -3712, 5941,  5961}},
            {"Micron 2010",                   110, 0,        /* DJC */
                    {16695, -3761,  -2151, 155,   9682,  163,   3433,  951,   4904}},
            {"Minolta DiMAGE 5",              0,   0xf7d,
                    {8983,  -2942,  -963,  -6556, 14476, 2237,  -2426, 2887,  8014}},
            {"Minolta DiMAGE 7Hi",            0,   0xf7d,
                    {11368, -3894,  -1242, -6521, 14358, 2339,  -2475, 3056,  7285}},
            {"Minolta DiMAGE 7",              0,   0xf7d,
                    {9144,  -2777,  -998,  -6676, 14556, 2281,  -2470, 3019,  7744}},
            {"Minolta DiMAGE A1",             0,   0xf8b,
                    {9274,  -2547,  -1167, -8220, 16323, 1943,  -2273, 2720,  8340}},
            {"Minolta DiMAGE A200",           0,   0,
                    {8560,  -2487,  -986,  -8112, 15535, 2771,  -1209, 1324,  7743}},
            {"Minolta DiMAGE A2",             0,   0xf8f,
                    {9097,  -2726,  -1053, -8073, 15506, 2762,  -966,  981,   7763}},
            {"Minolta DiMAGE Z2",             0,   0,    /* DJC */
                    {11280, -3564,  -1370, -4655, 12374, 2282,  -1423, 2168,  5396}},
            {"Minolta DYNAX 5",               0,   0xffb,
                    {10284, -3283,  -1086, -7957, 15762, 2316,  -829,  882,   6644}},
            {"Minolta DYNAX 7",               0,   0xffb,
                    {10239, -3104,  -1099, -8037, 15727, 2451,  -927,  925,   6871}},
            {"Motorola PIXL",                 0,   0,        /* DJC */
                    {8898,  -989,   -1033, -3292, 11619, 1674,  -661,  3178,  5216}},
            {"Nikon D100",                    0,   0,
                    {5902,  -933,   -782,  -8983, 16719, 2354,  -1402, 1455,  6464}},
            {"Nikon D1H",                     0,   0,
                    {7577,  -2166,  -926,  -7454, 15592, 1934,  -2377, 2808,  8606}},
            {"Nikon D1X",                     0,   0,
                    {7702,  -2245,  -975,  -9114, 17242, 1875,  -2679, 3055,  8521}},
            {"Nikon D1",                      0,   0, /* multiplied by 2.218750, 1.0, 1.148438 */
                    {16772, -4726,  -2141, -7611, 15713, 1972,  -2846, 3494,  9521}},
            {"Nikon D200",                    0,   0xfbc,
                    {8367,  -2248,  -763,  -8758, 16447, 2422,  -1527, 1550,  8053}},
            {"Nikon D2H",                     0,   0,
                    {5710,  -901,   -615,  -8594, 16617, 2024,  -2975, 4120,  6830}},
            {"Nikon D2X",                     0,   0,
                    {10231, -2769,  -1255, -8301, 15900, 2552,  -797,  680,   7148}},
            {"Nikon D3000",                   0,   0,
                    {8736,  -2458,  -935,  -9075, 16894, 2251,  -1354, 1242,  8263}},
            {"Nikon D3100",                   0,   0,
                    {7911,  -2167,  -813,  -5327, 13150, 2408,  -1288, 2483,  7968}},
            {"Nikon D3200",                   0,   0xfb9,
                    {7013,  -1408,  -635,  -5268, 12902, 2640,  -1470, 2801,  7379}},
            {"Nikon D3300",                   0,   0,
                    {6988,  -1384,  -714,  -5631, 13410, 2447,  -1485, 2204,  7318}},
            {"Nikon D3400",                   0,   0,
                    {6988,  -1384,  -714,  -5631, 13410, 2447,  -1485, 2204,  7318}},
            {"Nikon D300",                    0,   0,
                    {9030,  -1992,  -715,  -8465, 16302, 2255,  -2689, 3217,  8069}},
            {"Nikon D3X",                     0,   0,
                    {7171,  -1986,  -648,  -8085, 15555, 2718,  -2170, 2512,  7457}},
            {"Nikon D3S",                     0,   0,
                    {8828,  -2406,  -694,  -4874, 12603, 2541,  -660,  1509,  7587}},
            {"Nikon D3",                      0,   0,
                    {8139,  -2171,  -663,  -8747, 16541, 2295,  -1925, 2008,  8093}},
            {"Nikon D40X",                    0,   0,
                    {8819,  -2543,  -911,  -9025, 16928, 2151,  -1329, 1213,  8449}},
            {"Nikon D40",                     0,   0,
                    {6992,  -1668,  -806,  -8138, 15748, 2543,  -874,  850,   7897}},
            {"Nikon D4S",                     0,   0,
                    {8598,  -2848,  -857,  -5618, 13606, 2195,  -1002, 1773,  7137}},
            {"Nikon D4",                      0,   0,
                    {8598,  -2848,  -857,  -5618, 13606, 2195,  -1002, 1773,  7137}},
            {"Nikon Df",                      0,   0,
                    {8598,  -2848,  -857,  -5618, 13606, 2195,  -1002, 1773,  7137}},
            {"Nikon D5000",                   0,   0xf00,
                    {7309,  -1403,  -519,  -8474, 16008, 2622,  -2433, 2826,  8064}},
            {"Nikon D5100",                   0,   0x3de6,
                    {8198,  -2239,  -724,  -4871, 12389, 2798,  -1043, 2050,  7181}},
            {"Nikon D5200",                   0,   0,
                    {8322,  -3112,  -1047, -6367, 14342, 2179,  -988,  1638,  6394}},
            {"Nikon D5300",                   0,   0,
                    {6988,  -1384,  -714,  -5631, 13410, 2447,  -1485, 2204,  7318}},
            {"Nikon D5500",                   0,   0,
                    {8821,  -2938,  -785,  -4178, 12142, 2287,  -824,  1651,  6860}},
            {"Nikon D5600",                   0,   0,
                    {8821,  -2938,  -785,  -4178, 12142, 2287,  -824,  1651,  6860}},
            {"Nikon D500",                    0,   0,
                    {8813,  -3210,  -1036, -4703, 12868, 2021,  -1054, 1940,  6129}},
            {"Nikon D50",                     0,   0,
                    {7732,  -2422,  -789,  -8238, 15884, 2498,  -859,  783,   7330}},
            {"Nikon D5",                      0,   0,
                    {9200,  -3522,  -992,  -5755, 13803, 2117,  -753,  1486,  6338}},
            {"Nikon D600",                    0,   0x3e07,
                    {8178,  -2245,  -609,  -4857, 12394, 2776,  -1207, 2086,  7298}},
            {"Nikon D610",                    0,   0,
                    {8178,  -2245,  -609,  -4857, 12394, 2776,  -1207, 2086,  7298}},
            {"Nikon D60",                     0,   0,
                    {8736,  -2458,  -935,  -9075, 16894, 2251,  -1354, 1242,  8263}},
            {"Nikon D7000",                   0,   0,
                    {8198,  -2239,  -724,  -4871, 12389, 2798,  -1043, 2050,  7181}},
            {"Nikon D7100",                   0,   0,
                    {8322,  -3112,  -1047, -6367, 14342, 2179,  -988,  1638,  6394}},
            {"Nikon D7200",                   0,   0,
                    {8322,  -3112,  -1047, -6367, 14342, 2179,  -988,  1638,  6394}},
            {"Nikon D7500",                   0,   0,
                    {8813,  -3210,  -1036, -4703, 12868, 2021,  -1054, 1940,  6129}},
            {"Nikon D750",                    0,   0,
                    {9020,  -2890,  -715,  -4535, 12436, 2348,  -934,  1919,  7086}},
            {"Nikon D700",                    0,   0,
                    {8139,  -2171,  -663,  -8747, 16541, 2295,  -1925, 2008,  8093}},
            {"Nikon D70",                     0,   0,
                    {7732,  -2422,  -789,  -8238, 15884, 2498,  -859,  783,   7330}},
            {"Nikon D850",                    0,   0,
                    {10405, -3755,  -1270, -5461, 13787, 1793,  -1040, 2015,  6785}},
            {"Nikon D810",                    0,   0,
                    {9369,  -3195,  -791,  -4488, 12430, 2301,  -893,  1796,  6872}},
            {"Nikon D800",                    0,   0,
                    {7866,  -2108,  -555,  -4869, 12483, 2681,  -1176, 2069,  7501}},
            {"Nikon D80",                     0,   0,
                    {8629,  -2410,  -883,  -9055, 16940, 2171,  -1490, 1363,  8520}},
            {"Nikon D90",                     0,   0xf00,
                    {7309,  -1403,  -519,  -8474, 16008, 2622,  -2434, 2826,  8064}},
            {"Nikon E700",                    0,   0x3dd,        /* DJC */
                    {-3746, 10611,  1665,  9621,  -1734, 2114,  -2389, 7082,  3064, 3406,  6116,  -244}},
            {"Nikon E800",                    0,   0x3dd,        /* DJC */
                    {-3746, 10611,  1665,  9621,  -1734, 2114,  -2389, 7082,  3064, 3406,  6116,  -244}},
            {"Nikon E950",                    0,   0x3dd,        /* DJC */
                    {-3746, 10611,  1665,  9621,  -1734, 2114,  -2389, 7082,  3064, 3406,  6116,  -244}},
            {"Nikon E995",                    0,   0,    /* copied from E5000 */
                    {-5547, 11762,  2189,  5814,  -558,  3342,  -4924, 9840,  5949, 688,   9083,  96}},
            {"Nikon E2100",                   0,   0,    /* copied from Z2, new white balance */
                    {13142, -4152,  -1596, -4655, 12374, 2282,  -1769, 2696,  6711}},
            {"Nikon E2500",                   0,   0,
                    {-5547, 11762,  2189,  5814,  -558,  3342,  -4924, 9840,  5949, 688,   9083,  96}},
            {"Nikon E3200",                   0,   0,        /* DJC */
                    {9846,  -2085,  -1019, -3278, 11109, 2170,  -774,  2134,  5745}},
            {"Nikon E4300",                   0,   0,    /* copied from Minolta DiMAGE Z2 */
                    {11280, -3564,  -1370, -4655, 12374, 2282,  -1423, 2168,  5396}},
            {"Nikon E4500",                   0,   0,
                    {-5547, 11762,  2189,  5814,  -558,  3342,  -4924, 9840,  5949, 688,   9083,  96}},
            {"Nikon E5000",                   0,   0,
                    {-5547, 11762,  2189,  5814,  -558,  3342,  -4924, 9840,  5949, 688,   9083,  96}},
            {"Nikon E5400",                   0,   0,
                    {9349,  -2987,  -1001, -7919, 15766, 2266,  -2098, 2680,  6839}},
            {"Nikon E5700",                   0,   0,
                    {-5368, 11478,  2368,  5537,  -113,  3148,  -4969, 10021, 5782, 778,   9028,  211}},
            {"Nikon E8400",                   0,   0,
                    {7842,  -2320,  -992,  -8154, 15718, 2599,  -1098, 1342,  7560}},
            {"Nikon E8700",                   0,   0,
                    {8489,  -2583,  -1036, -8051, 15583, 2643,  -1307, 1407,  7354}},
            {"Nikon E8800",                   0,   0,
                    {7971,  -2314,  -913,  -8451, 15762, 2894,  -1442, 1520,  7610}},
            {"Nikon COOLPIX A",               0,   0,
                    {8198,  -2239,  -724,  -4871, 12389, 2798,  -1043, 2050,  7181}},
            {"Nikon COOLPIX B700",            200, 0,
                    {14387, -6014,  -1299, -1357, 9975,  1616,  467,   1047,  4744}},
            {"Nikon COOLPIX P330",            200, 0,
                    {10321, -3920,  -931,  -2750, 11146, 1824,  -442,  1545,  5539}},
            {"Nikon COOLPIX P340",            200, 0,
                    {10321, -3920,  -931,  -2750, 11146, 1824,  -442,  1545,  5539}},
            {"Nikon COOLPIX P6000",           0,   0,
                    {9698,  -3367,  -914,  -4706, 12584, 2368,  -837,  968,   5801}},
            {"Nikon COOLPIX P7000",           0,   0,
                    {11432, -3679,  -1111, -3169, 11239, 2202,  -791,  1380,  4455}},
            {"Nikon COOLPIX P7100",           0,   0,
                    {11053, -4269,  -1024, -1976, 10182, 2088,  -526,  1263,  4469}},
            {"Nikon COOLPIX P7700",           200, 0,
                    {10321, -3920,  -931,  -2750, 11146, 1824,  -442,  1545,  5539}},
            {"Nikon COOLPIX P7800",           200, 0,
                    {10321, -3920,  -931,  -2750, 11146, 1824,  -442,  1545,  5539}},
            {"Nikon 1 V3",                    0,   0,
                    {5958,  -1559,  -571,  -4021, 11453, 2939,  -634,  1548,  5087}},
            {"Nikon 1 J4",                    0,   0,
                    {5958,  -1559,  -571,  -4021, 11453, 2939,  -634,  1548,  5087}},
            {"Nikon 1 J5",                    0,   0,
                    {7520,  -2518,  -645,  -3844, 12102, 1945,  -913,  2249,  6835}},
            {"Nikon 1 S2",                    200, 0,
                    {6612,  -1342,  -618,  -3338, 11055, 2623,  -174,  1792,  5075}},
            {"Nikon 1 V2",                    0,   0,
                    {6588,  -1305,  -693,  -3277, 10987, 2634,  -355,  2016,  5106}},
            {"Nikon 1 J3",                    0,   0,
                    {6588,  -1305,  -693,  -3277, 10987, 2634,  -355,  2016,  5106}},
            {"Nikon 1 AW1",                   0,   0,
                    {6588,  -1305,  -693,  -3277, 10987, 2634,  -355,  2016,  5106}},
            {"Nikon 1 ",                      0,   0,        /* J1, J2, S1, V1 */
                    {8994,  -2667,  -865,  -4594, 12324, 2552,  -699,  1786,  6260}},
            {"Olympus AIR A01",               0,   0,
                    {8992,  -3093,  -639,  -2563, 10721, 2122,  -437,  1270,  5473}},
            {"Olympus C5050",                 0,   0,
                    {10508, -3124,  -1273, -6079, 14294, 1901,  -1653, 2306,  6237}},
            {"Olympus C5060",                 0,   0,
                    {10445, -3362,  -1307, -7662, 15690, 2058,  -1135, 1176,  7602}},
            {"Olympus C7070",                 0,   0,
                    {10252, -3531,  -1095, -7114, 14850, 2436,  -1451, 1723,  6365}},
            {"Olympus C70",                   0,   0,
                    {10793, -3791,  -1146, -7498, 15177, 2488,  -1390, 1577,  7321}},
            {"Olympus C80",                   0,   0,
                    {8606,  -2509,  -1014, -8238, 15714, 2703,  -942,  979,   7760}},
            {"Olympus E-10",                  0,   0xffc,
                    {12745, -4500,  -1416, -6062, 14542, 1580,  -1934, 2256,  6603}},
            {"Olympus E-1",                   0,   0,
                    {11846, -4767,  -945,  -7027, 15878, 1089,  -2699, 4122,  8311}},
            {"Olympus E-20",                  0,   0xffc,
                    {13173, -4732,  -1499, -5807, 14036, 1895,  -2045, 2452,  7142}},
            {"Olympus E-300",                 0,   0,
                    {7828,  -1761,  -348,  -5788, 14071, 1830,  -2853, 4518,  6557}},
            {"Olympus E-330",                 0,   0,
                    {8961,  -2473,  -1084, -7979, 15990, 2067,  -2319, 3035,  8249}},
            {"Olympus E-30",                  0,   0xfbc,
                    {8144,  -1861,  -1111, -7763, 15894, 1929,  -1865, 2542,  7607}},
            {"Olympus E-3",                   0,   0xf99,
                    {9487,  -2875,  -1115, -7533, 15606, 2010,  -1618, 2100,  7389}},
            {"Olympus E-400",                 0,   0,
                    {6169,  -1483,  -21,   -7107, 14761, 2536,  -2904, 3580,  8568}},
            {"Olympus E-410",                 0,   0xf6a,
                    {8856,  -2582,  -1026, -7761, 15766, 2082,  -2009, 2575,  7469}},
            {"Olympus E-420",                 0,   0xfd7,
                    {8746,  -2425,  -1095, -7594, 15612, 2073,  -1780, 2309,  7416}},
            {"Olympus E-450",                 0,   0xfd2,
                    {8745,  -2425,  -1095, -7594, 15613, 2073,  -1780, 2309,  7416}},
            {"Olympus E-500",                 0,   0,
                    {8136,  -1968,  -299,  -5481, 13742, 1871,  -2556, 4205,  6630}},
            {"Olympus E-510",                 0,   0xf6a,
                    {8785,  -2529,  -1033, -7639, 15624, 2112,  -1783, 2300,  7817}},
            {"Olympus E-520",                 0,   0xfd2,
                    {8344,  -2322,  -1020, -7596, 15635, 2048,  -1748, 2269,  7287}},
            {"Olympus E-5",                   0,   0xeec,
                    {11200, -3783,  -1325, -4576, 12593, 2206,  -695,  1742,  7504}},
            {"Olympus E-600",                 0,   0xfaf,
                    {8453,  -2198,  -1092, -7609, 15681, 2008,  -1725, 2337,  7824}},
            {"Olympus E-620",                 0,   0xfaf,
                    {8453,  -2198,  -1092, -7609, 15681, 2008,  -1725, 2337,  7824}},
            {"Olympus E-P1",                  0,   0xffd,
                    {8343,  -2050,  -1021, -7715, 15705, 2103,  -1831, 2380,  8235}},
            {"Olympus E-P2",                  0,   0xffd,
                    {8343,  -2050,  -1021, -7715, 15705, 2103,  -1831, 2380,  8235}},
            {"Olympus E-P3",                  0,   0,
                    {7575,  -2159,  -571,  -3722, 11341, 2725,  -1434, 2819,  6271}},
            {"Olympus E-P5",                  0,   0,
                    {8380,  -2630,  -639,  -2887, 10725, 2496,  -627,  1427,  5438}},
            {"Olympus E-PL1s",                0,   0,
                    {11409, -3872,  -1393, -4572, 12757, 2003,  -709,  1810,  7415}},
            {"Olympus E-PL1",                 0,   0,
                    {11408, -4289,  -1215, -4286, 12385, 2118,  -387,  1467,  7787}},
            {"Olympus E-PL2",                 0,   0xcf3,
                    {15030, -5552,  -1806, -3987, 12387, 1767,  -592,  1670,  7023}},
            {"Olympus E-PL3",                 0,   0,
                    {7575,  -2159,  -571,  -3722, 11341, 2725,  -1434, 2819,  6271}},
            {"Olympus E-PL5",                 0,   0xfcb,
                    {8380,  -2630,  -639,  -2887, 10725, 2496,  -627,  1427,  5438}},
            {"Olympus E-PL6",                 0,   0,
                    {8380,  -2630,  -639,  -2887, 10725, 2496,  -627,  1427,  5438}},
            {"Olympus E-PL7",                 0,   0,
                    {9197,  -3190,  -659,  -2606, 10830, 2039,  -458,  1250,  5458}},
            {"Olympus E-PL8",                 0,   0,
                    {9197,  -3190,  -659,  -2606, 10830, 2039,  -458,  1250,  5458}},
            {"Olympus E-PL9",                 0,   0,
                    {8380,  -2630,  -639,  -2887, 10725, 2496,  -627,  1427,  5438}},
            {"Olympus E-PM1",                 0,   0,
                    {7575,  -2159,  -571,  -3722, 11341, 2725,  -1434, 2819,  6271}},
            {"Olympus E-PM2",                 0,   0,
                    {8380,  -2630,  -639,  -2887, 10725, 2496,  -627,  1427,  5438}},
            {"Olympus E-M10",                 0,   0,    /* also E-M10 Mark II & III */
                    {8380,  -2630,  -639,  -2887, 10725, 2496,  -627,  1427,  5438}},
            {"Olympus E-M1Mark II",           0,   0,
                    {9383,  -3170,  -763,  -2457, 10702, 2020,  -384,  1236,  5552}},
            {"Olympus E-M1",                  0,   0,
                    {7687,  -1984,  -606,  -4327, 11928, 2721,  -1381, 2339,  6452}},
            {"Olympus E-M5MarkII",            0,   0,
                    {9422,  -3258,  -711,  -2655, 10898, 2015,  -512,  1354,  5512}},
            {"Olympus E-M5",                  0,   0xfe1,
                    {8380,  -2630,  -639,  -2887, 10725, 2496,  -627,  1427,  5438}},
            {"Olympus PEN-F",                 0,   0,
                    {9476,  -3182,  -765,  -2613, 10958, 1893,  -449,  1315,  5268}},
            {"Olympus SH-2",                  0,   0,
                    {10156, -3425,  -1077, -2611, 11177, 1624,  -385,  1592,  5080}},
            {"Olympus SP350",                 0,   0,
                    {12078, -4836,  -1069, -6671, 14306, 2578,  -786,  939,   7418}},
            {"Olympus SP3",                   0,   0,
                    {11766, -4445,  -1067, -6901, 14421, 2707,  -1029, 1217,  7572}},
            {"Olympus SP500UZ",               0,   0xfff,
                    {9493,  -3415,  -666,  -5211, 12334, 3260,  -1548, 2262,  6482}},
            {"Olympus SP510UZ",               0,   0xffe,
                    {10593, -3607,  -1010, -5881, 13127, 3084,  -1200, 1805,  6721}},
            {"Olympus SP550UZ",               0,   0xffe,
                    {11597, -4006,  -1049, -5432, 12799, 2957,  -1029, 1750,  6516}},
            {"Olympus SP560UZ",               0,   0xff9,
                    {10915, -3677,  -982,  -5587, 12986, 2911,  -1168, 1968,  6223}},
            {"Olympus SP570UZ",               0,   0,
                    {11522, -4044,  -1146, -4736, 12172, 2904,  -988,  1829,  6039}},
            {"Olympus STYLUS1",               0,   0,
                    {8360,  -2420,  -880,  -3928, 12353, 1739,  -1381, 2416,  5173}},
            {"Olympus TG-4",                  0,   0,
                    {11426, -4159,  -1126, -2066, 10678, 1593,  -120,  1327,  4998}},
            {"Olympus TG-5",                  0,   0,
                    {10899, -3833,  -1082, -2112, 10736, 1575,  -267,  1452,  5269}},
            {"Olympus XZ-10",                 0,   0,
                    {9777,  -3483,  -925,  -2886, 11297, 1800,  -602,  1663,  5134}},
            {"Olympus XZ-1",                  0,   0,
                    {10901, -4095,  -1074, -1141, 9208,  2293,  -62,   1417,  5158}},
            {"Olympus XZ-2",                  0,   0,
                    {9777,  -3483,  -925,  -2886, 11297, 1800,  -602,  1663,  5134}},
            {"OmniVision",                    0,   0,        /* DJC */
                    {12782, -4059,  -379,  -478,  9066,  1413,  1340,  1513,  5176}},
            {"Pentax *ist DL2",               0,   0,
                    {10504, -2438,  -1189, -8603, 16207, 2531,  -1022, 863,   12242}},
            {"Pentax *ist DL",                0,   0,
                    {10829, -2838,  -1115, -8339, 15817, 2696,  -837,  680,   11939}},
            {"Pentax *ist DS2",               0,   0,
                    {10504, -2438,  -1189, -8603, 16207, 2531,  -1022, 863,   12242}},
            {"Pentax *ist DS",                0,   0,
                    {10371, -2333,  -1206, -8688, 16231, 2602,  -1230, 1116,  11282}},
            {"Pentax *ist D",                 0,   0,
                    {9651,  -2059,  -1189, -8881, 16512, 2487,  -1460, 1345,  10687}},
            {"Pentax K10D",                   0,   0,
                    {9566,  -2863,  -803,  -7170, 15172, 2112,  -818,  803,   9705}},
            {"Pentax K1",                     0,   0,
                    {11095, -3157,  -1324, -8377, 15834, 2720,  -1108, 947,   11688}},
            {"Pentax K20D",                   0,   0,
                    {9427,  -2714,  -868,  -7493, 16092, 1373,  -2199, 3264,  7180}},
            {"Pentax K200D",                  0,   0,
                    {9186,  -2678,  -907,  -8693, 16517, 2260,  -1129, 1094,  8524}},
            {"Pentax K2000",                  0,   0,
                    {11057, -3604,  -1155, -5152, 13046, 2329,  -282,  375,   8104}},
            {"Pentax K-m",                    0,   0,
                    {11057, -3604,  -1155, -5152, 13046, 2329,  -282,  375,   8104}},
            {"Pentax K-x",                    0,   0,
                    {8843,  -2837,  -625,  -5025, 12644, 2668,  -411,  1234,  7410}},
            {"Pentax K-r",                    0,   0,
                    {9895,  -3077,  -850,  -5304, 13035, 2521,  -883,  1768,  6936}},
            {"Pentax K-1",                    0,   0,
                    {8596,  -2981,  -639,  -4202, 12046, 2431,  -685,  1424,  6122}},
            {"Pentax K-30",                   0,   0,
                    {8710,  -2632,  -1167, -3995, 12301, 1881,  -981,  1719,  6535}},
            {"Pentax K-3 II",                 0,   0,
                    {8626,  -2607,  -1155, -3995, 12301, 1881,  -1039, 1822,  6925}},
            {"Pentax K-3",                    0,   0,
                    {7415,  -2052,  -721,  -5186, 12788, 2682,  -1446, 2157,  6773}},
            {"Pentax K-5 II",                 0,   0,
                    {8170,  -2725,  -639,  -4440, 12017, 2744,  -771,  1465,  6599}},
            {"Pentax K-5",                    0,   0,
                    {8713,  -2833,  -743,  -4342, 11900, 2772,  -722,  1543,  6247}},
            {"Pentax K-70",                   0,   0,
                    {8270,  -2117,  -1299, -4359, 12953, 1515,  -1078, 1933,  5975}},
            {"Pentax K-7",                    0,   0,
                    {9142,  -2947,  -678,  -8648, 16967, 1663,  -2224, 2898,  8615}},
            {"Pentax K-S1",                   0,   0,
                    {8512,  -3211,  -787,  -4167, 11966, 2487,  -638,  1288,  6054}},
            {"Pentax K-S2",                   0,   0,
                    {8662,  -3280,  -798,  -3928, 11771, 2444,  -586,  1232,  6054}},
            {"Pentax KP",                     0,   0,
                    {8617,  -3228,  -1034, -4674, 12821, 2044,  -803,  1577,  5728}},
            {"Pentax Q-S1",                   0,   0,
                    {12995, -5593,  -1107, -1879, 10139, 2027,  -64,   1233,  4919}},
            {"Pentax 645D",                   0,   0x3e00,
                    {10646, -3593,  -1158, -3329, 11699, 1831,  -667,  2874,  6287}},
            {"Panasonic DMC-CM1",             15,  0,
                    {8770,  -3194,  -820,  -2871, 11281, 1803,  -513,  1552,  4434}},
            {"Panasonic DC-FZ80",             0,   0,
                    {8550,  -2908,  -842,  -3195, 11529, 1881,  -338,  1603,  4631}},
            {"Panasonic DMC-FZ8",             0,   0xf7f,
                    {8986,  -2755,  -802,  -6341, 13575, 3077,  -1476, 2144,  6379}},
            {"Panasonic DMC-FZ18",            0,   0,
                    {9932,  -3060,  -935,  -5809, 13331, 2753,  -1267, 2155,  5575}},
            {"Panasonic DMC-FZ28",            15,  0xf96,
                    {10109, -3488,  -993,  -5412, 12812, 2916,  -1305, 2140,  5543}},
            {"Panasonic DMC-FZ2500",          15,  0,
                    {7386,  -2443,  -743,  -3437, 11864, 1757,  -608,  1660,  4766}},
            {"Panasonic DMC-FZ330",           15,  0,
                    {8378,  -2798,  -769,  -3068, 11410, 1877,  -538,  1792,  4623}},
            {"Panasonic DMC-FZ300",           15,  0,
                    {8378,  -2798,  -769,  -3068, 11410, 1877,  -538,  1792,  4623}},
            {"Panasonic DMC-FZ30",            0,   0xf94,
                    {10976, -4029,  -1141, -7918, 15491, 2600,  -1670, 2071,  8246}},
            {"Panasonic DMC-FZ3",             15,  0,    /* FZ35, FZ38 */
                    {9938,  -2780,  -890,  -4604, 12393, 2480,  -1117, 2304,  4620}},
            {"Panasonic DMC-FZ4",             15,  0,    /* FZ40, FZ45 */
                    {13639, -5535,  -1371, -1698, 9633,  2430,  316,   1152,  4108}},
            {"Panasonic DMC-FZ50",            0,   0,
                    {7906,  -2709,  -594,  -6231, 13351, 3220,  -1922, 2631,  6537}},
            {"Panasonic DMC-FZ7",             15,  0,    /* FZ70, FZ72 */
                    {11532, -4324,  -1066, -2375, 10847, 1749,  -564,  1699,  4351}},
            {"Leica V-LUX1",                  0,   0,
                    {7906,  -2709,  -594,  -6231, 13351, 3220,  -1922, 2631,  6537}},
            {"Panasonic DMC-L10",             15,  0xf96,
                    {8025,  -1942,  -1050, -7920, 15904, 2100,  -2456, 3005,  7039}},
            {"Panasonic DMC-L1",              0,   0xf7f,
                    {8054,  -1885,  -1025, -8349, 16367, 2040,  -2805, 3542,  7629}},
            {"Leica DIGILUX 3",               0,   0xf7f,
                    {8054,  -1885,  -1025, -8349, 16367, 2040,  -2805, 3542,  7629}},
            {"Panasonic DMC-LC1",             0,   0,
                    {11340, -4069,  -1275, -7555, 15266, 2448,  -2960, 3426,  7685}},
            {"Leica DIGILUX 2",               0,   0,
                    {11340, -4069,  -1275, -7555, 15266, 2448,  -2960, 3426,  7685}},
            {"Panasonic DMC-LX100",           15,  0,
                    {8844,  -3538,  -768,  -3709, 11762, 2200,  -698,  1792,  5220}},
            {"Leica D-LUX (Typ 109)",         15,  0,
                    {8844,  -3538,  -768,  -3709, 11762, 2200,  -698,  1792,  5220}},
            {"Panasonic DMC-LF1",             15,  0,
                    {9379,  -3267,  -816,  -3227, 11560, 1881,  -926,  1928,  5340}},
            {"Leica C (Typ 112)",             15,  0,
                    {9379,  -3267,  -816,  -3227, 11560, 1881,  -926,  1928,  5340}},
            {"Panasonic DMC-LX1",             0,   0xf7f,
                    {10704, -4187,  -1230, -8314, 15952, 2501,  -920,  945,   8927}},
            {"Leica D-LUX2",                  0,   0xf7f,
                    {10704, -4187,  -1230, -8314, 15952, 2501,  -920,  945,   8927}},
            {"Panasonic DMC-LX2",             0,   0,
                    {8048,  -2810,  -623,  -6450, 13519, 3272,  -1700, 2146,  7049}},
            {"Leica D-LUX3",                  0,   0,
                    {8048,  -2810,  -623,  -6450, 13519, 3272,  -1700, 2146,  7049}},
            {"Panasonic DMC-LX3",             15,  0,
                    {8128,  -2668,  -655,  -6134, 13307, 3161,  -1782, 2568,  6083}},
            {"Leica D-LUX 4",                 15,  0,
                    {8128,  -2668,  -655,  -6134, 13307, 3161,  -1782, 2568,  6083}},
            {"Panasonic DMC-LX5",             15,  0,
                    {10909, -4295,  -948,  -1333, 9306,  2399,  22,    1738,  4582}},
            {"Leica D-LUX 5",                 15,  0,
                    {10909, -4295,  -948,  -1333, 9306,  2399,  22,    1738,  4582}},
            {"Panasonic DMC-LX7",             15,  0,
                    {10148, -3743,  -991,  -2837, 11366, 1659,  -701,  1893,  4899}},
            {"Leica D-LUX 6",                 15,  0,
                    {10148, -3743,  -991,  -2837, 11366, 1659,  -701,  1893,  4899}},
            {"Panasonic DMC-LX9",             15,  0,
                    {7790,  -2736,  -755,  -3452, 11870, 1769,  -628,  1647,  4898}},
            {"Panasonic DMC-FZ1000",          15,  0,
                    {7830,  -2696,  -763,  -3325, 11667, 1866,  -641,  1712,  4824}},
            {"Leica V-LUX (Typ 114)",         15,  0,
                    {7830,  -2696,  -763,  -3325, 11667, 1866,  -641,  1712,  4824}},
            {"Panasonic DMC-FZ100",           15,  0xfff,
                    {16197, -6146,  -1761, -2393, 10765, 1869,  366,   2238,  5248}},
            {"Leica V-LUX 2",                 15,  0xfff,
                    {16197, -6146,  -1761, -2393, 10765, 1869,  366,   2238,  5248}},
            {"Panasonic DMC-FZ150",           15,  0xfff,
                    {11904, -4541,  -1189, -2355, 10899, 1662,  -296,  1586,  4289}},
            {"Leica V-LUX 3",                 15,  0xfff,
                    {11904, -4541,  -1189, -2355, 10899, 1662,  -296,  1586,  4289}},
            {"Panasonic DMC-FZ200",           15,  0xfff,
                    {8112,  -2563,  -740,  -3730, 11784, 2197,  -941,  2075,  4933}},
            {"Leica V-LUX 4",                 15,  0xfff,
                    {8112,  -2563,  -740,  -3730, 11784, 2197,  -941,  2075,  4933}},
            {"Panasonic DMC-FX150",           15,  0xfff,
                    {9082,  -2907,  -925,  -6119, 13377, 3058,  -1797, 2641,  5609}},
            {"Panasonic DMC-G10",             0,   0,
                    {10113, -3400,  -1114, -4765, 12683, 2317,  -377,  1437,  6710}},
            {"Panasonic DMC-G1",              15,  0xf94,
                    {8199,  -2065,  -1056, -8124, 16156, 2033,  -2458, 3022,  7220}},
            {"Panasonic DMC-G2",              15,  0xf3c,
                    {10113, -3400,  -1114, -4765, 12683, 2317,  -377,  1437,  6710}},
            {"Panasonic DMC-G3",              15,  0xfff,
                    {6763,  -1919,  -863,  -3868, 11515, 2684,  -1216, 2387,  5879}},
            {"Panasonic DMC-G5",              15,  0xfff,
                    {7798,  -2562,  -740,  -3879, 11584, 2613,  -1055, 2248,  5434}},
            {"Panasonic DMC-G6",              15,  0xfff,
                    {8294,  -2891,  -651,  -3869, 11590, 2595,  -1183, 2267,  5352}},
            {"Panasonic DMC-G7",              15,  0xfff,
                    {7610,  -2780,  -576,  -4614, 12195, 2733,  -1375, 2393,  6490}},
            {"Panasonic DMC-G8",              15,  0xfff,    /* G8, G80, G81, G85 */
                    {7610,  -2780,  -576,  -4614, 12195, 2733,  -1375, 2393,  6490}},
            {"Panasonic DC-G9",               15,  0xfff,
                    {7685,  -2375,  -634,  -3687, 11700, 2249,  -748,  1546,  5111}},
            {"Panasonic DMC-GF1",             15,  0xf92,
                    {7888,  -1902,  -1011, -8106, 16085, 2099,  -2353, 2866,  7330}},
            {"Panasonic DMC-GF2",             15,  0xfff,
                    {7888,  -1902,  -1011, -8106, 16085, 2099,  -2353, 2866,  7330}},
            {"Panasonic DMC-GF3",             15,  0xfff,
                    {9051,  -2468,  -1204, -5212, 13276, 2121,  -1197, 2510,  6890}},
            {"Panasonic DMC-GF5",             15,  0xfff,
                    {8228,  -2945,  -660,  -3938, 11792, 2430,  -1094, 2278,  5793}},
            {"Panasonic DMC-GF6",             15,  0,
                    {8130,  -2801,  -946,  -3520, 11289, 2552,  -1314, 2511,  5791}},
            {"Panasonic DMC-GF7",             15,  0,
                    {7610,  -2780,  -576,  -4614, 12195, 2733,  -1375, 2393,  6490}},
            {"Panasonic DMC-GF8",             15,  0,
                    {7610,  -2780,  -576,  -4614, 12195, 2733,  -1375, 2393,  6490}},
            {"Panasonic DC-GF9",              15,  0,
                    {7610,  -2780,  -576,  -4614, 12195, 2733,  -1375, 2393,  6490}},
            {"Panasonic DMC-GH1",             15,  0xf92,
                    {6299,  -1466,  -532,  -6535, 13852, 2969,  -2331, 3112,  5984}},
            {"Panasonic DMC-GH2",             15,  0xf95,
                    {7780,  -2410,  -806,  -3913, 11724, 2484,  -1018, 2390,  5298}},
            {"Panasonic DMC-GH3",             15,  0,
                    {6559,  -1752,  -491,  -3672, 11407, 2586,  -962,  1875,  5130}},
            {"Panasonic DMC-GH4",             15,  0,
                    {7122,  -2108,  -512,  -3155, 11201, 2231,  -541,  1423,  5045}},
            {"Panasonic DC-GH5S",             15,  0,
                    {6929,  -2355,  -708,  -4192, 12534, 1828,  -1097, 1989,  5195}},
            {"Panasonic DC-GH5",              15,  0,
                    {7641,  -2336,  -605,  -3218, 11299, 2187,  -485,  1338,  5121}},
            {"Panasonic DMC-GM1",             15,  0,
                    {6770,  -1895,  -744,  -5232, 13145, 2303,  -1664, 2691,  5703}},
            {"Panasonic DMC-GM5",             15,  0,
                    {8238,  -3244,  -679,  -3921, 11814, 2384,  -836,  2022,  5852}},
            {"Panasonic DMC-GX1",             15,  0,
                    {6763,  -1919,  -863,  -3868, 11515, 2684,  -1216, 2387,  5879}},
            {"Panasonic DMC-GX7",             15,  0,
                    {7610,  -2780,  -576,  -4614, 12195, 2733,  -1375, 2393,  6490}},
            {"Panasonic DMC-GX85",            15,  0,
                    {7771,  -3020,  -629,  -4029, 11950, 2345,  -821,  1977,  6119}},
            {"Panasonic DMC-GX8",             15,  0,
                    {7564,  -2263,  -606,  -3148, 11239, 2177,  -540,  1435,  4853}},
            {"Panasonic DC-GX9",              15,  0,
                    {7564,  -2263,  -606,  -3148, 11239, 2177,  -540,  1435,  4853}},
            {"Panasonic DMC-ZS100",           15,  0,
                    {7790,  -2736,  -755,  -3452, 11870, 1769,  -628,  1647,  4898}},
            {"Panasonic DC-ZS200",            15,  0,
                    {7790,  -2736,  -755,  -3452, 11870, 1769,  -628,  1647,  4898}},
            {"Panasonic DMC-ZS40",            15,  0,
                    {8607,  -2822,  -808,  -3755, 11930, 2049,  -820,  2060,  5224}},
            {"Panasonic DMC-ZS50",            15,  0,
                    {8802,  -3135,  -789,  -3151, 11468, 1904,  -550,  1745,  4810}},
            {"Panasonic DMC-TZ82",            15,  0,
                    {8550,  -2908,  -842,  -3195, 11529, 1881,  -338,  1603,  4631}},
            {"Panasonic DMC-ZS6",             15,  0,
                    {8550,  -2908,  -842,  -3195, 11529, 1881,  -338,  1603,  4631}},
            {"Panasonic DMC-ZS70",            15,  0,
                    {9052,  -3117,  -883,  -3045, 11346, 1927,  -205,  1520,  4730}},
            {"Leica S (Typ 007)",             0,   0,
                    {6063,  -2234,  -231,  -5210, 13787, 1500,  -1043, 2866,  6997}},
            {"Leica X",                       0,   0,        /* X and X-U, both (Typ 113) */
                    {7712,  -2059,  -653,  -3882, 11494, 2726,  -710,  1332,  5958}},
            {"Leica Q (Typ 116)",             0,   0,
                    {11865, -4523,  -1441, -5423, 14458, 935,   -1587, 2687,  4830}},
            {"Leica M (Typ 262)",             0,   0,
                    {6653,  -1486,  -611,  -4221, 13303, 929,   -881,  2416,  7226}},
            {"Leica SL (Typ 601)",            0,   0,
                    {11865, -4523,  -1441, -5423, 14458, 935,   -1587, 2687,  4830}},
            {"Leica TL2",                     0,   0,
                    {5836,  -1626,  -647,  -5384, 13326, 2261,  -1207, 2129,  5861}},
            {"Leica TL",                      0,   0,
                    {5463,  -988,   -364,  -4634, 12036, 2946,  -766,  1389,  6522}},
            {"Leica CL",                      0,   0,
                    {7414,  -2393,  -840,  -5127, 13180, 2138,  -1585, 2468,  5064}},
            {"Leica M10",                     0,   0,
                    {8249,  -2849,  -620,  -5415, 14756, 565,   -957,  3074,  6517}},
            {"Phase One H 20",                0,   0,        /* DJC */
                    {1313,  1855,   -109,  -6715, 15908, 808,   -327,  1840,  6020}},
            {"Phase One H 25",                0,   0,
                    {2905,  732,    -237,  -8134, 16626, 1476,  -3038, 4253,  7517}},
            {"Phase One P 2",                 0,   0,
                    {2905,  732,    -237,  -8134, 16626, 1476,  -3038, 4253,  7517}},
            {"Phase One P 30",                0,   0,
                    {4516,  -245,   -37,   -7020, 14976, 2173,  -3206, 4671,  7087}},
            {"Phase One P 45",                0,   0,
                    {5053,  -24,    -117,  -5684, 14076, 1702,  -2619, 4492,  5849}},
            {"Phase One P40",                 0,   0,
                    {8035,  435,    -962,  -6001, 13872, 2320,  -1159, 3065,  5434}},
            {"Phase One P65",                 0,   0,
                    {8035,  435,    -962,  -6001, 13872, 2320,  -1159, 3065,  5434}},
            {"Photron BC2-HD",                0,   0,        /* DJC */
                    {14603, -4122,  -528,  -1810, 9794,  2017,  -297,  2763,  5936}},
            {"Red One",                       704, (short)0xffff,        /* DJC */
                    {21014, -7891,  -2613, -3056, 12201, 856,   -2203, 5125,  8042}},
            {"Ricoh GR II",                   0,   0,
                    {4630,  -834,   -423,  -4977, 12805, 2417,  -638,  1467,  6115}},
            {"Ricoh GR",                      0,   0,
                    {3708,  -543,   -160,  -5381, 12254, 3556,  -1471, 1929,  8234}},
            {"Samsung EX1",                   0,   0x3e00,
                    {8898,  -2498,  -994,  -3144, 11328, 2066,  -760,  1381,  4576}},
            {"Samsung EX2F",                  0,   0x7ff,
                    {10648, -3897,  -1055, -2022, 10573, 1668,  -492,  1611,  4742}},
            {"Samsung EK-GN120",              0,   0,
                    {7557,  -2522,  -739,  -4679, 12949, 1894,  -840,  1777,  5311}},
            {"Samsung NX mini",               0,   0,
                    {5222,  -1196,  -550,  -6540, 14649, 2009,  -1666, 2819,  5657}},
            {"Samsung NX3300",                0,   0,
                    {8060,  -2933,  -761,  -4504, 12890, 1762,  -630,  1489,  5227}},
            {"Samsung NX3000",                0,   0,
                    {8060,  -2933,  -761,  -4504, 12890, 1762,  -630,  1489,  5227}},
            {"Samsung NX30",                  0,   0,    /* NX30, NX300, NX300M */
                    {7557,  -2522,  -739,  -4679, 12949, 1894,  -840,  1777,  5311}},
            {"Samsung NX2000",                0,   0,
                    {7557,  -2522,  -739,  -4679, 12949, 1894,  -840,  1777,  5311}},
            {"Samsung NX2",                   0,   0xfff,    /* NX20, NX200, NX210 */
                    {6933,  -2268,  -753,  -4921, 13387, 1647,  -803,  1641,  6096}},
            {"Samsung NX1000",                0,   0,
                    {6933,  -2268,  -753,  -4921, 13387, 1647,  -803,  1641,  6096}},
            {"Samsung NX1100",                0,   0,
                    {6933,  -2268,  -753,  -4921, 13387, 1647,  -803,  1641,  6096}},
            {"Samsung NX11",                  0,   0,
                    {10332, -3234,  -1168, -6111, 14639, 1520,  -1352, 2647,  8331}},
            {"Samsung NX10",                  0,   0,    /* also NX100 */
                    {10332, -3234,  -1168, -6111, 14639, 1520,  -1352, 2647,  8331}},
            {"Samsung NX500",                 0,   0,
                    {10686, -4042,  -1052, -3595, 13238, 276,   -464,  1259,  5931}},
            {"Samsung NX5",                   0,   0,
                    {10332, -3234,  -1168, -6111, 14639, 1520,  -1352, 2647,  8331}},
            {"Samsung NX1",                   0,   0,
                    {10686, -4042,  -1052, -3595, 13238, 276,   -464,  1259,  5931}},
            {"Samsung WB2000",                0,   0xfff,
                    {12093, -3557,  -1155, -1000, 9534,  1733,  -22,   1787,  4576}},
            {"Samsung GX-1",                  0,   0,
                    {10504, -2438,  -1189, -8603, 16207, 2531,  -1022, 863,   12242}},
            {"Samsung GX20",                  0,   0,    /* copied from Pentax K20D */
                    {9427,  -2714,  -868,  -7493, 16092, 1373,  -2199, 3264,  7180}},
            {"Samsung S85",                   0,   0,        /* DJC */
                    {11885, -3968,  -1473, -4214, 12299, 1916,  -835,  1655,  5549}},
            {"Sinar",                         0,   0,            /* DJC */
                    {16442, -2956,  -2422, -2877, 12128, 750,   -1136, 6066,  4559}},
            {"Sony DSC-F828",                 0,   0,
                    {7924,  -1910,  -777,  -8226, 15459, 2998,  -1517, 2199,  6818, -7242, 11401, 3481}},
            {"Sony DSC-R1",                   0,   0,
                    {8512,  -2641,  -694,  -8042, 15670, 2526,  -1821, 2117,  7414}},
            {"Sony DSC-V3",                   0,   0,
                    {7511,  -2571,  -692,  -7894, 15088, 3060,  -948,  1111,  8128}},
            {"Sony DSC-RX100M",               0,   0,        /* M2, M3, M4, and M5 */
                    {6596,  -2079,  -562,  -4782, 13016, 1933,  -970,  1581,  5181}},
            {"Sony DSC-RX100",                0,   0,
                    {8651,  -2754,  -1057, -3464, 12207, 1373,  -568,  1398,  4434}},
            {"Sony DSC-RX10M4",               0,   0,
                    {7699,  -2566,  -629,  -2967, 11270, 1928,  -378,  1286,  4807}},
            {"Sony DSC-RX10",                 0,   0,        /* also RX10M2, RX10M3 */
                    {6679,  -1825,  -745,  -5047, 13256, 1953,  -1580, 2422,  5183}},
            {"Sony DSC-RX1RM2",               0,   0,
                    {6629,  -1900,  -483,  -4618, 12349, 2550,  -622,  1381,  6514}},
            {"Sony DSC-RX1",                  0,   0,
                    {6344,  -1612,  -462,  -4863, 12477, 2681,  -865,  1786,  6899}},
            {"Sony DSC-RX0",                  200, 0,
                    {9396,  -3507,  -843,  -2497, 11111, 1572,  -343,  1355,  5089}},
            {"Sony DSLR-A100",                0,   0xfeb,
                    {9437,  -2811,  -774,  -8405, 16215, 2290,  -710,  596,   7181}},
            {"Sony DSLR-A290",                0,   0,
                    {6038,  -1484,  -579,  -9145, 16746, 2512,  -875,  746,   7218}},
            {"Sony DSLR-A2",                  0,   0,
                    {9847,  -3091,  -928,  -8485, 16345, 2225,  -715,  595,   7103}},
            {"Sony DSLR-A300",                0,   0,
                    {9847,  -3091,  -928,  -8485, 16345, 2225,  -715,  595,   7103}},
            {"Sony DSLR-A330",                0,   0,
                    {9847,  -3091,  -929,  -8485, 16346, 2225,  -714,  595,   7103}},
            {"Sony DSLR-A350",                0,   0xffc,
                    {6038,  -1484,  -578,  -9146, 16746, 2513,  -875,  746,   7217}},
            {"Sony DSLR-A380",                0,   0,
                    {6038,  -1484,  -579,  -9145, 16746, 2512,  -875,  746,   7218}},
            {"Sony DSLR-A390",                0,   0,
                    {6038,  -1484,  -579,  -9145, 16746, 2512,  -875,  746,   7218}},
            {"Sony DSLR-A450",                0,   0xfeb,
                    {4950,  -580,   -103,  -5228, 12542, 3029,  -709,  1435,  7371}},
            {"Sony DSLR-A580",                0,   0xfeb,
                    {5932,  -1492,  -411,  -4813, 12285, 2856,  -741,  1524,  6739}},
            {"Sony DSLR-A500",                0,   0xfeb,
                    {6046,  -1127,  -278,  -5574, 13076, 2786,  -691,  1419,  7625}},
            {"Sony DSLR-A5",                  0,   0xfeb,
                    {4950,  -580,   -103,  -5228, 12542, 3029,  -709,  1435,  7371}},
            {"Sony DSLR-A700",                0,   0,
                    {5775,  -805,   -359,  -8574, 16295, 2391,  -1943, 2341,  7249}},
            {"Sony DSLR-A850",                0,   0,
                    {5413,  -1162,  -365,  -5665, 13098, 2866,  -608,  1179,  8440}},
            {"Sony DSLR-A900",                0,   0,
                    {5209,  -1072,  -397,  -8845, 16120, 2919,  -1618, 1803,  8654}},
            {"Sony ILCA-68",                  0,   0,
                    {6435,  -1903,  -536,  -4722, 12449, 2550,  -663,  1363,  6517}},
            {"Sony ILCA-77M2",                0,   0,
                    {5991,  -1732,  -443,  -4100, 11989, 2381,  -704,  1467,  5992}},
            {"Sony ILCA-99M2",                0,   0,
                    {6660,  -1918,  -471,  -4613, 12398, 2485,  -649,  1433,  6447}},
            {"Sony ILCE-6",                   0,   0,        /* 6300, 6500 */
                    {5973,  -1695,  -419,  -3826, 11797, 2293,  -639,  1398,  5789}},
            {"Sony ILCE-7M2",                 0,   0,
                    {5271,  -712,   -347,  -6153, 13653, 2763,  -1601, 2366,  7242}},
            {"Sony ILCE-7M3",                 0,   0,
                    {7374,  -2389,  -551,  -5435, 13162, 2519,  -1006, 1795,  6552}},
            {"Sony ILCE-7S",                  0,   0,    /* also ILCE-7SM2 */
                    {5838,  -1430,  -246,  -3497, 11477, 2297,  -748,  1885,  5778}},
            {"Sony ILCE-7RM3",                0,   0,
                    {6640,  -1847,  -503,  -5238, 13010, 2474,  -993,  1673,  6527}},
            {"Sony ILCE-7RM2",                0,   0,
                    {6629,  -1900,  -483,  -4618, 12349, 2550,  -622,  1381,  6514}},
            {"Sony ILCE-7R",                  0,   0,
                    {4913,  -541,   -202,  -6130, 13513, 2906,  -1564, 2151,  7183}},
            {"Sony ILCE-7",                   0,   0,
                    {5271,  -712,   -347,  -6153, 13653, 2763,  -1601, 2366,  7242}},
            {"Sony ILCE-9",                   0,   0,
                    {6389,  -1703,  -378,  -4562, 12265, 2587,  -670,  1489,  6550}},
            {"Sony ILCE",                     0,   0,    /* 3000, 5000, 5100, 6000, and QX1 */
                    {5991,  -1456,  -455,  -4764, 12135, 2980,  -707,  1425,  6701}},
            {"Sony NEX-5N",                   0,   0,
                    {5991,  -1456,  -455,  -4764, 12135, 2980,  -707,  1425,  6701}},
            {"Sony NEX-5R",                   0,   0,
                    {6129,  -1545,  -418,  -4930, 12490, 2743,  -977,  1693,  6615}},
            {"Sony NEX-5T",                   0,   0,
                    {6129,  -1545,  -418,  -4930, 12490, 2743,  -977,  1693,  6615}},
            {"Sony NEX-3N",                   0,   0,
                    {6129,  -1545,  -418,  -4930, 12490, 2743,  -977,  1693,  6615}},
            {"Sony NEX-3",                    138, 0,        /* DJC */
                    {6907,  -1256,  -645,  -4940, 12621, 2320,  -1710, 2581,  6230}},
            {"Sony NEX-5",                    116, 0,        /* DJC */
                    {6807,  -1350,  -342,  -4216, 11649, 2567,  -1089, 2001,  6420}},
            {"Sony NEX-3",                    0,   0,        /* Adobe */
                    {6549,  -1550,  -436,  -4880, 12435, 2753,  -854,  1868,  6976}},
            {"Sony NEX-5",                    0,   0,        /* Adobe */
                    {6549,  -1550,  -436,  -4880, 12435, 2753,  -854,  1868,  6976}},
            {"Sony NEX-6",                    0,   0,
                    {6129,  -1545,  -418,  -4930, 12490, 2743,  -977,  1693,  6615}},
            {"Sony NEX-7",                    0,   0,
                    {5491,  -1192,  -363,  -4951, 12342, 2948,  -911,  1722,  7192}},
            {"Sony NEX",                      0,   0,    /* NEX-C3, NEX-F3 */
                    {5991,  -1456,  -455,  -4764, 12135, 2980,  -707,  1425,  6701}},
            {"Sony SLT-A33",                  0,   0,
                    {6069,  -1221,  -366,  -5221, 12779, 2734,  -1024, 2066,  6834}},
            {"Sony SLT-A35",                  0,   0,
                    {5986,  -1618,  -415,  -4557, 11820, 3120,  -681,  1404,  6971}},
            {"Sony SLT-A37",                  0,   0,
                    {5991,  -1456,  -455,  -4764, 12135, 2980,  -707,  1425,  6701}},
            {"Sony SLT-A55",                  0,   0,
                    {5932,  -1492,  -411,  -4813, 12285, 2856,  -741,  1524,  6739}},
            {"Sony SLT-A57",                  0,   0,
                    {5991,  -1456,  -455,  -4764, 12135, 2980,  -707,  1425,  6701}},
            {"Sony SLT-A58",                  0,   0,
                    {5991,  -1456,  -455,  -4764, 12135, 2980,  -707,  1425,  6701}},
            {"Sony SLT-A65",                  0,   0,
                    {5491,  -1192,  -363,  -4951, 12342, 2948,  -911,  1722,  7192}},
            {"Sony SLT-A77",                  0,   0,
                    {5491,  -1192,  -363,  -4951, 12342, 2948,  -911,  1722,  7192}},
            {"Sony SLT-A99",                  0,   0,
                    {6344,  -1612,  -462,  -4863, 12477, 2681,  -865,  1786,  6899}},
            {"YI M1",                         0,   0,
                    {7712,  -2059,  -653,  -3882, 11494, 2726,  -710,  1332,  5958}},
    };
    double cam_xyz[4][3];
    char name[130];
    int i;
    int j;

    snprintf(name, 130, "%s %s", adobeMake, adobeModel);
    for ( i = 0; i < sizeof table / sizeof *table; i++ ) {
        if ( !strncmp(name, table[i].prefix, strlen(table[i].prefix))) {
            if ( table[i].black ) {
                ADOBE_black = (unsigned short) table[i].black;
            }
            if ( table[i].maximum ) {
                ADOBE_maximum = (unsigned short) table[i].maximum;
            }
            if ( table[i].trans[0] ) {
                for ( GLOBAL_colorTransformForRaw = j = 0; j < 12; j++ )
                    ((double *) cam_xyz)[j] = table[i].trans[j] / 10000.0;
                cam_xyz_coeff(rgb_cam, cam_xyz);
            }
            break;
        }
    }
}

void
simple_coeff(int index) {
    static const float table[][12] = {
        // index 0 -- all Foveon cameras
        {1.4032,    -0.2231,  -0.1016,   -0.5263, 1.4816,   0.017,     -0.0112,   0.0183, 0.9113},
        // index 1 -- Kodak DC20 and DC25
        {2.25,      0.75,     -1.75,     -0.25,   -0.25,    0.75,      0.75,      -0.25,  -0.25,     -1.75,    0.75,     2.25},
        // index 2 -- Logitech Fotoman Pixtura
        {1.893,     -0.418,   -0.476,    -0.495,  1.773,    -0.278,    -1.017,    -0.655, 2.672},
        // index 3 -- Nikon E880, E900, and E990
        {-1.936280, 1.800443, -1.448486, 2.584324,1.405365, -0.524955, -0.289090, 0.408680, -1.204965, 1.082304, 2.941367, -1.818705}
    };
    int i;
    int c;

    for ( GLOBAL_colorTransformForRaw = i = 0; i < 3; i++ ) {
        for ( c = 0; c < IMAGE_colors; c++ ) {
            rgb_cam[i][c] = table[index][i * IMAGE_colors + c];
        }
    }
}

short
guess_byte_order(int words) {
    unsigned char test[4][2];
    int t = 2, msb;
    double diff;
    double sum[2] = {0, 0};

    fread(test[0], 2, 2, GLOBAL_IO_ifp);
    for ( words -= 2; words--; ) {
        fread(test[t], 2, 1, GLOBAL_IO_ifp);
        for ( msb = 0; msb < 2; msb++ ) {
            diff = (test[t ^ 2][msb] << 8 | test[t ^ 2][!msb])
                   - (test[t][msb] << 8 | test[t][!msb]);
            sum[msb] += diff * diff;
        }
        t = (t + 1) & 3;
    }
    return sum[0] < sum[1] ? BIG_ENDIAN_ORDER : LITTLE_ENDIAN_ORDER;
}

float
find_green(int bps, int bite, int off0, int off1) {
    UINT64 bitbuf = 0;
    int vbits;
    int col;
    int i;
    int c;
    unsigned short img[2][2064];
    double sum[] = {0, 0};

    for ( c = 0; c < 2; c++ ) {
        fseek(GLOBAL_IO_ifp, c ? off1 : off0, SEEK_SET);
        for ( vbits = col = 0; col < width; col++ ) {
            for ( vbits -= bps; vbits < 0; vbits += bite ) {
                bitbuf <<= bite;
                for ( i = 0; i < bite; i += 8 ) {
                    bitbuf |= (unsigned) (fgetc(GLOBAL_IO_ifp) << i);
                }
            }
            img[c][col] = bitbuf << (64 - bps - vbits) >> (64 - bps);
        }
    }
    for ( c = 0; c < width - 1; c++ ) {
        sum[c & 1] += ABS(img[0][c] - img[1][c + 1]);
        sum[~c & 1] += ABS(img[1][c] - img[0][c + 1]);
    }
    return 100.0 * log(sum[0] / sum[1]);
}

/*
Identify which camera created this file, and set global variables
accordingly.
*/
void
tiffIdentify() {
    static const short pana[][6] = {
            {3130, 1743, 4,  0,  -6,  0},
            {3130, 2055, 4,  0,  -6,  0},
            {3130, 2319, 4,  0,  -6,  0},
            {3170, 2103, 18, 0,  -42, 20},
            {3170, 2367, 18, 13, -42, -21},
            {3177, 2367, 0,  0,  -1,  0},
            {3304, 2458, 0,  0,  -1,  0},
            {3330, 2463, 9,  0,  -5,  0},
            {3330, 2479, 9,  0,  -17, 4},
            {3370, 1899, 15, 0,  -44, 20},
            {3370, 2235, 15, 0,  -44, 20},
            {3370, 2511, 15, 10, -44, -21},
            {3690, 2751, 3,  0,  -8,  -3},
            {3710, 2751, 0,  0,  -3,  0},
            {3724, 2450, 0,  0,  0,   -2},
            {3770, 2487, 17, 0,  -44, 19},
            {3770, 2799, 17, 15, -44, -19},
            {3880, 2170, 6,  0,  -6,  0},
            {4060, 3018, 0,  0,  0,   -2},
            {4290, 2391, 3,  0,  -8,  -1},
            {4330, 2439, 17, 15, -44, -19},
            {4508, 2962, 0,  0,  -3,  -4},
            {4508, 3330, 0,  0,  -3,  -6},
    };
    static const unsigned short canon[][11] = {
            {1944, 1416, 0,   0,   48, 0},
            {2144, 1560, 4,   8,   52, 2,  0, 0,  0, 25},
            {2224, 1456, 48,  6,   0,  2},
            {2376, 1728, 12,  6,   52, 2},
            {2672, 1968, 12,  6,   44, 2},
            {3152, 2068, 64,  12,  0,  0,  16},
            {3160, 2344, 44,  12,  4,  4},
            {3344, 2484, 4,   6,   52, 6},
            {3516, 2328, 42,  14,  0,  0},
            {3596, 2360, 74,  12,  0,  0},
            {3744, 2784, 52,  12,  8,  12},
            {3944, 2622, 30,  18,  6,  2},
            {3948, 2622, 42,  18,  0,  2},
            {3984, 2622, 76,  20,  0,  2,  14},
            {4104, 3048, 48,  12,  24, 12},
            {4116, 2178, 4,   2,   0,  0},
            {4152, 2772, 192, 12,  0,  0},
            {4160, 3124, 104, 11,  8,  65},
            {4176, 3062, 96,  17,  8,  0,  0, 16, 0, 7,  0x49},
            {4192, 3062, 96,  17,  24, 0,  0, 16, 0, 0,  0x49},
            {4312, 2876, 22,  18,  0,  2},
            {4352, 2874, 62,  18,  0,  0},
            {4476, 2954, 90,  34,  0,  0},
            {4480, 3348, 12,  10,  36, 12, 0, 0,  0, 18, 0x49},
            {4480, 3366, 80,  50,  0,  0},
            {4496, 3366, 80,  50,  12, 0},
            {4768, 3516, 96,  16,  0,  0,  0, 16},
            {4832, 3204, 62,  26,  0,  0},
            {4832, 3228, 62,  51,  0,  0},
            {5108, 3349, 98,  13,  0,  0},
            {5120, 3318, 142, 45,  62, 0},
            {5280, 3528, 72,  52,  0,  0},
            {5344, 3516, 142, 51,  0,  0},
            {5344, 3584, 126, 100, 0,  2},
            {5360, 3516, 158, 51,  0,  0},
            {5568, 3708, 72,  38,  0,  0},
            {5632, 3710, 96,  17,  0,  0,  0, 16, 0, 0,  0x49},
            {5712, 3774, 62,  20,  10, 2},
            {5792, 3804, 158, 51,  0,  0},
            {5920, 3950, 122, 80,  2,  0},
            {6096, 4051, 76,  35,  0,  0},
            {6096, 4056, 72,  34,  0,  0},
            {6288, 4056, 264, 36,  0,  0},
            {6384, 4224, 120, 44,  0,  0},
            {6880, 4544, 136, 42,  0,  0},
            {8896, 5920, 160, 64,  0,  0},
    };
    static const struct {
        unsigned short id;
        char model[20];
    } unique[] = {
            {0x168, "EOS 10D"},
            {0x001, "EOS-1D"},
            {0x175, "EOS 20D"},
            {0x174, "EOS-1D Mark II"},
            {0x234, "EOS 30D"},
            {0x232, "EOS-1D Mark II N"},
            {0x190, "EOS 40D"},
            {0x169, "EOS-1D Mark III"},
            {0x261, "EOS 50D"},
            {0x281, "EOS-1D Mark IV"},
            {0x287, "EOS 60D"},
            {0x167, "EOS-1DS"},
            {0x325, "EOS 70D"},
            {0x408, "EOS 77D"},
            {0x331, "EOS M"},
            {0x350, "EOS 80D"},
            {0x328, "EOS-1D X Mark II"},
            {0x346, "EOS 100D"},
            {0x417, "EOS 200D"},
            {0x170, "EOS 300D"},
            {0x188, "EOS-1Ds Mark II"},
            {0x176, "EOS 450D"},
            {0x215, "EOS-1Ds Mark III"},
            {0x189, "EOS 350D"},
            {0x324, "EOS-1D C"},
            {0x236, "EOS 400D"},
            {0x269, "EOS-1D X"},
            {0x252, "EOS 500D"},
            {0x213, "EOS 5D"},
            {0x270, "EOS 550D"},
            {0x218, "EOS 5D Mark II"},
            {0x286, "EOS 600D"},
            {0x285, "EOS 5D Mark III"},
            {0x301, "EOS 650D"},
            {0x302, "EOS 6D"},
            {0x326, "EOS 700D"},
            {0x250, "EOS 7D"},
            {0x393, "EOS 750D"},
            {0x289, "EOS 7D Mark II"},
            {0x347, "EOS 760D"},
            {0x406, "EOS 6D Mark II"},
            {0x405, "EOS 800D"},
            {0x349, "EOS 5D Mark IV"},
            {0x254, "EOS 1000D"},
            {0x288, "EOS 1100D"},
            {0x327, "EOS 1200D"},
            {0x382, "EOS 5DS"},
            {0x404, "EOS 1300D"},
            {0x401, "EOS 5DS R"},
            {0x422, "EOS 1500D"},
            {0x432, "EOS 3000D"},
    }, sonique[] = {
            {0x002, "DSC-R1"},
            {0x100, "DSLR-A100"},
            {0x101, "DSLR-A900"},
            {0x102, "DSLR-A700"},
            {0x103, "DSLR-A200"},
            {0x104, "DSLR-A350"},
            {0x105, "DSLR-A300"},
            {0x108, "DSLR-A330"},
            {0x109, "DSLR-A230"},
            {0x10a, "DSLR-A290"},
            {0x10d, "DSLR-A850"},
            {0x111, "DSLR-A550"},
            {0x112, "DSLR-A500"},
            {0x113, "DSLR-A450"},
            {0x116, "NEX-5"},
            {0x117, "NEX-3"},
            {0x118, "SLT-A33"},
            {0x119, "SLT-A55V"},
            {0x11a, "DSLR-A560"},
            {0x11b, "DSLR-A580"},
            {0x11c, "NEX-C3"},
            {0x11d, "SLT-A35"},
            {0x11e, "SLT-A65V"},
            {0x11f, "SLT-A77V"},
            {0x120, "NEX-5N"},
            {0x121, "NEX-7"},
            {0x123, "SLT-A37"},
            {0x124, "SLT-A57"},
            {0x125, "NEX-F3"},
            {0x126, "SLT-A99V"},
            {0x127, "NEX-6"},
            {0x128, "NEX-5R"},
            {0x129, "DSC-RX100"},
            {0x12a, "DSC-RX1"},
            {0x12e, "ILCE-3000"},
            {0x12f, "SLT-A58"},
            {0x131, "NEX-3N"},
            {0x132, "ILCE-7"},
            {0x133, "NEX-5T"},
            {0x134, "DSC-RX100M2"},
            {0x135, "DSC-RX10"},
            {0x136, "DSC-RX1R"},
            {0x137, "ILCE-7R"},
            {0x138, "ILCE-6000"},
            {0x139, "ILCE-5000"},
            {0x13d, "DSC-RX100M3"},
            {0x13e, "ILCE-7S"},
            {0x13f, "ILCA-77M2"},
            {0x153, "ILCE-5100"},
            {0x154, "ILCE-7M2"},
            {0x155, "DSC-RX100M4"},
            {0x156, "DSC-RX10M2"},
            {0x158, "DSC-RX1RM2"},
            {0x15a, "ILCE-QX1"},
            {0x15b, "ILCE-7RM2"},
            {0x15e, "ILCE-7SM2"},
            {0x161, "ILCA-68"},
            {0x162, "ILCA-99M2"},
            {0x163, "DSC-RX10M3"},
            {0x164, "DSC-RX100M5"},
            {0x165, "ILCE-6300"},
            {0x166, "ILCE-9"},
            {0x168, "ILCE-6500"},
            {0x16a, "ILCE-7RM3"},
            {0x16b, "ILCE-7M3"},
            {0x16c, "DSC-RX0"},
            {0x16d, "DSC-RX10M4"},
    };
    static const char *orig;
    static const char panalias[][12] = {
            "@DC-FZ80", "DC-FZ82", "DC-FZ85",
            "@DC-FZ81", "DC-FZ83",
            "@DC-GF9", "DC-GX800", "DC-GX850",
            "@DC-GF10", "DC-GF90",
            "@DC-GX9", "DC-GX7MK3",
            "@DC-ZS70", "DC-TZ90", "DC-TZ91", "DC-TZ92", "DC-TZ93",
            "@DMC-FZ40", "DMC-FZ45",
            "@DMC-FZ2500", "DMC-FZ2000", "DMC-FZH1",
            "@DMC-G8", "DMC-G80", "DMC-G81", "DMC-G85",
            "@DMC-GX85", "DMC-GX80", "DMC-GX7MK2",
            "@DMC-LX9", "DMC-LX10", "DMC-LX15",
            "@DMC-ZS40", "DMC-TZ60", "DMC-TZ61",
            "@DMC-ZS50", "DMC-TZ70", "DMC-TZ71",
            "@DMC-ZS60", "DMC-TZ80", "DMC-TZ81", "DMC-TZ85",
            "@DMC-ZS100", "DMC-ZS110", "DMC-TZ100", "DMC-TZ101", "DMC-TZ110", "DMC-TX1",
            "@DC-ZS200", "DC-TX2", "DC-TZ200", "DC-TZ202", "DC-TZ220", "DC-ZS220",
    };
    static const struct {
        unsigned fsize;
        unsigned short rw, rh;
        unsigned char lm, tm, rm, bm, lf, cf, max, flags;
        char make[10], model[20];
        unsigned short offset;
    } table[] = {
            {786432,   1024, 768,  0,  0,  0,  0,  0,   0x94, 0, 0,  "AVT",       "F-080C"},
            {1447680,  1392, 1040, 0,  0,  0,  0,  0,   0x94, 0, 0,  "AVT",       "F-145C"},
            {1920000,  1600, 1200, 0,  0,  0,  0,  0,   0x94, 0, 0,  "AVT",       "F-201C"},
            {5067304,  2588, 1958, 0,  0,  0,  0,  0,   0x94, 0, 0,  "AVT",       "F-510C"},
            {5067316,  2588, 1958, 0,  0,  0,  0,  0,   0x94, 0, 0,  "AVT",       "F-510C", 12},
            {10134608, 2588, 1958, 0,  0,  0,  0,  9,   0x94, 0, 0,  "AVT",       "F-510C"},
            {10134620, 2588, 1958, 0,  0,  0,  0,  9,   0x94, 0, 0,  "AVT",       "F-510C", 12},
            {16157136, 3272, 2469, 0,  0,  0,  0,  9,   0x94, 0, 0,  "AVT",       "F-810C"},
            {15980544, 3264, 2448, 0,  0,  0,  0,  8,   0x61, 0, 1,  "AgfaPhoto", "DC-833m"},
            {9631728,  2532, 1902, 0,  0,  0,  0,  96,  0x61, 0, 0,  "Alcatel",   "5035D"},
            {2868726,  1384, 1036, 0,  0,  0,  0,  64,  0x49, 0, 8,  "Baumer",    "TXG14",  1078},
            {5298000,  2400, 1766, 12, 12, 44, 2,  8,   0x94, 0, 2,  "Canon",     "PowerShot SD300"},
            {6553440,  2664, 1968, 4,  4,  44, 4,  8,   0x94, 0, 2,  "Canon",     "PowerShot A460"},
            {6573120,  2672, 1968, 12, 8,  44, 0,  8,   0x94, 0, 2,  "Canon",     "PowerShot A610"},
            {6653280,  2672, 1992, 10, 6,  42, 2,  8,   0x94, 0, 2,  "Canon",     "PowerShot A530"},
            {7710960,  2888, 2136, 44, 8,  4,  0,  8,   0x94, 0, 2,  "Canon",     "PowerShot S3 IS"},
            {9219600,  3152, 2340, 36, 12, 4,  0,  8,   0x94, 0, 2,  "Canon",     "PowerShot A620"},
            {9243240,  3152, 2346, 12, 7,  44, 13, 8,   0x49, 0, 2,  "Canon",     "PowerShot A470"},
            {10341600, 3336, 2480, 6,  5,  32, 3,  8,   0x94, 0, 2,  "Canon",     "PowerShot A720 IS"},
            {10383120, 3344, 2484, 12, 6,  44, 6,  8,   0x94, 0, 2,  "Canon",     "PowerShot A630"},
            {12945240, 3736, 2772, 12, 6,  52, 6,  8,   0x94, 0, 2,  "Canon",     "PowerShot A640"},
            {15636240, 4104, 3048, 48, 12, 24, 12, 8,   0x94, 0, 2,  "Canon",     "PowerShot A650"},
            {15467760, 3720, 2772, 6,  12, 30, 0,  8,   0x94, 0, 2,  "Canon",     "PowerShot SX110 IS"},
            {15534576, 3728, 2778, 12, 9,  44, 9,  8,   0x94, 0, 2,  "Canon",     "PowerShot SX120 IS"},
            {18653760, 4080, 3048, 24, 12, 24, 12, 8,   0x94, 0, 2,  "Canon",     "PowerShot SX20 IS"},
            {19131120, 4168, 3060, 92, 16, 4,  1,  8,   0x94, 0, 2,  "Canon",     "PowerShot SX220 HS"},
            {21936096, 4464, 3276, 25, 10, 73, 12, 8,   0x16, 0, 2,  "Canon",     "PowerShot SX30 IS"},
            {24724224, 4704, 3504, 8,  16, 56, 8,  8,   0x94, 0, 2,  "Canon",     "PowerShot A3300 IS"},
            {30858240, 5248, 3920, 8,  16, 56, 16, 8,   0x94, 0, 2,  "Canon",     "IXUS 160"},
            {1976352,  1632, 1211, 0,  2,  0,  1,  0,   0x94, 0, 1,  "Casio",     "QV-2000UX"},
            {3217760,  2080, 1547, 0,  0,  10, 1,  0,   0x94, 0, 1,  "Casio",     "QV-3*00EX"},
            {6218368,  2585, 1924, 0,  0,  9,  0,  0,   0x94, 0, 1,  "Casio",     "QV-5700"},
            {7816704,  2867, 2181, 0,  0,  34, 36, 0,   0x16, 0, 1,  "Casio",     "EX-Z60"},
            {2937856,  1621, 1208, 0,  0,  1,  0,  0,   0x94, 7, 13, "Casio",     "EX-S20"},
            {4948608,  2090, 1578, 0,  0,  32, 34, 0,   0x94, 7, 1,  "Casio",     "EX-S100"},
            {6054400,  2346, 1720, 2,  0,  32, 0,  0,   0x94, 7, 1,  "Casio",     "QV-R41"},
            {7426656,  2568, 1928, 0,  0,  0,  0,  0,   0x94, 0, 1,  "Casio",     "EX-P505"},
            {7530816,  2602, 1929, 0,  0,  22, 0,  0,   0x94, 7, 1,  "Casio",     "QV-R51"},
            {7542528,  2602, 1932, 0,  0,  32, 0,  0,   0x94, 7, 1,  "Casio",     "EX-Z50"},
            {7562048,  2602, 1937, 0,  0,  25, 0,  0,   0x16, 7, 1,  "Casio",     "EX-Z500"},
            {7753344,  2602, 1986, 0,  0,  32, 26, 0,   0x94, 7, 1,  "Casio",     "EX-Z55"},
            {9313536,  2858, 2172, 0,  0,  14, 30, 0,   0x94, 7, 1,  "Casio",     "EX-P600"},
            {10834368, 3114, 2319, 0,  0,  27, 0,  0,   0x94, 0, 1,  "Casio",     "EX-Z750"},
            {10843712, 3114, 2321, 0,  0,  25, 0,  0,   0x94, 0, 1,  "Casio",     "EX-Z75"},
            {10979200, 3114, 2350, 0,  0,  32, 32, 0,   0x94, 7, 1,  "Casio",     "EX-P700"},
            {12310144, 3285, 2498, 0,  0,  6,  30, 0,   0x94, 0, 1,  "Casio",     "EX-Z850"},
            {12489984, 3328, 2502, 0,  0,  47, 35, 0,   0x94, 0, 1,  "Casio",     "EX-Z8"},
            {15499264, 3754, 2752, 0,  0,  82, 0,  0,   0x94, 0, 1,  "Casio",     "EX-Z1050"},
            {18702336, 4096, 3044, 0,  0,  24, 0,  80,  0x94, 7, 1,  "Casio",     "EX-ZR100"},
            {7684000,  2260, 1700, 0,  0,  0,  0,  13,  0x94, 0, 1,  "Casio",     "QV-4000"},
            {787456,   1024, 769,  0,  1,  0,  0,  0,   0x49, 0, 0,  "Creative",  "PC-CAM 600"},
            {28829184, 4384, 3288, 0,  0,  0,  0,  36,  0x61, 0, 0,  "DJI"},
            {15151104, 4608, 3288, 0,  0,  0,  0,  0,   0x94, 0, 0,  "Matrix"},
            {3840000,  1600, 1200, 0,  0,  0,  0,  65,  0x49, 0, 0,  "Foculus",   "531C"},
            {307200,   640,  480,  0,  0,  0,  0,  0,   0x94, 0, 0,  "Generic"},
            {62464,    256,  244,  1,  1,  6,  1,  0,   0x8d, 0, 0,  "Kodak",     "DC20"},
            {124928,   512,  244,  1,  1,  10, 1,  0,   0x8d, 0, 0,  "Kodak",     "DC20"},
            {1652736,  1536, 1076, 0,  52, 0,  0,  0,   0x61, 0, 0,  "Kodak",     "DCS200"},
            {4159302,  2338, 1779, 1,  33, 1,  2,  0,   0x94, 0, 0,  "Kodak",     "C330"},
            {4162462,  2338, 1779, 1,  33, 1,  2,  0,   0x94, 0, 0,  "Kodak",     "C330",   3160},
            {2247168,  1232, 912,  0,  0,  16, 0,  0,   0x00, 0, 0,  "Kodak",     "C330"},
            {3370752,  1232, 912,  0,  0,  16, 0,  0,   0x00, 0, 0,  "Kodak",     "C330"},
            {6163328,  2864, 2152, 0,  0,  0,  0,  0,   0x94, 0, 0,  "Kodak",     "C603"},
            {6166488,  2864, 2152, 0,  0,  0,  0,  0,   0x94, 0, 0,  "Kodak",     "C603",   3160},
            {460800,   640,  480,  0,  0,  0,  0,  0,   0x00, 0, 0,  "Kodak",     "C603"},
            {9116448,  2848, 2134, 0,  0,  0,  0,  0,   0x00, 0, 0,  "Kodak",     "C603"},
            {12241200, 4040, 3030, 2,  0,  0,  13, 0,   0x49, 0, 0,  "Kodak",     "12MP"},
            {12272756, 4040, 3030, 2,  0,  0,  13, 0,   0x49, 0, 0,  "Kodak",     "12MP",   31556},
            {18000000, 4000, 3000, 0,  0,  0,  0,  0,   0x00, 0, 0,  "Kodak",     "12MP"},
            {614400,   640,  480,  0,  3,  0,  0,  64,  0x94, 0, 0,  "Kodak",     "KAI-0340"},
            {15360000, 3200, 2400, 0,  0,  0,  0,  96,  0x16, 0, 0,  "Lenovo",    "A820"},
            {3884928,  1608, 1207, 0,  0,  0,  0,  96,  0x16, 0, 0,  "Micron",    "2010",   3212},
            {1138688,  1534, 986,  0,  0,  0,  0,  0,   0x61, 0, 0,  "Minolta",   "RD175",  513},
            {1581060,  1305, 969,  0,  0,  18, 6,  6,   0x1e, 4, 1,  "Nikon",     "E900"},
            {2465792,  1638, 1204, 0,  0,  22, 1,  6,   0x4b, 5, 1,  "Nikon",     "E950"},
            {2940928,  1616, 1213, 0,  0,  0,  7,  30,  0x94, 0, 1,  "Nikon",     "E2100"},
            {4771840,  2064, 1541, 0,  0,  0,  1,  6,   0xe1, 0, 1,  "Nikon",     "E990"},
            {4775936,  2064, 1542, 0,  0,  0,  0,  30,  0x94, 0, 1,  "Nikon",     "E3700"},
            {5865472,  2288, 1709, 0,  0,  0,  1,  6,   0xb4, 0, 1,  "Nikon",     "E4500"},
            {5869568,  2288, 1710, 0,  0,  0,  0,  6,   0x16, 0, 1,  "Nikon",     "E4300"},
            {7438336,  2576, 1925, 0,  0,  0,  1,  6,   0xb4, 0, 1,  "Nikon",     "E5000"},
            {8998912,  2832, 2118, 0,  0,  0,  0,  30,  0x94, 7, 1,  "Nikon",     "COOLPIX S6"},
            {5939200,  2304, 1718, 0,  0,  0,  0,  30,  0x16, 0, 0,  "Olympus",   "C770UZ"},
            {3178560,  2064, 1540, 0,  0,  0,  0,  0,   0x94, 0, 1,  "Pentax",    "Optio S"},
            {4841984,  2090, 1544, 0,  0,  22, 0,  0,   0x94, 7, 1,  "Pentax",    "Optio S"},
            {6114240,  2346, 1737, 0,  0,  22, 0,  0,   0x94, 7, 1,  "Pentax",    "Optio S4"},
            {10702848, 3072, 2322, 0,  0,  0,  21, 30,  0x94, 0, 1,  "Pentax",    "Optio 750Z"},
            {4147200,  1920, 1080, 0,  0,  0,  0,  0,   0x49, 0, 0,  "Photron",   "BC2-HD"},
            {4151666,  1920, 1080, 0,  0,  0,  0,  0,   0x49, 0, 0,  "Photron",   "BC2-HD", 8},
            {13248000, 2208, 3000, 0,  0,  0,  0,  13,  0x61, 0, 0,  "Pixelink",  "A782"},
            {6291456,  2048, 1536, 0,  0,  0,  0,  96,  0x61, 0, 0,  "RoverShot", "3320AF"},
            {311696,   644,  484,  0,  0,  0,  0,  0,   0x16, 0, 8,  "ST Micro",  "STV680 VGA"},
            {16098048, 3288, 2448, 0,  0,  24, 0,  9,   0x94, 0, 1,  "Samsung",   "S85"},
            {16215552, 3312, 2448, 0,  0,  48, 0,  9,   0x94, 0, 1,  "Samsung",   "S85"},
            {20487168, 3648, 2808, 0,  0,  0,  0,  13,  0x94, 5, 1,  "Samsung",   "WB550"},
            {24000000, 4000, 3000, 0,  0,  0,  0,  13,  0x94, 5, 1,  "Samsung",   "WB550"},
            {12582980, 3072, 2048, 0,  0,  0,  0,  33,  0x61, 0, 0,  "Sinar",     "",       68},
            {33292868, 4080, 4080, 0,  0,  0,  0,  33,  0x61, 0, 0,  "Sinar",     "",       68},
            {44390468, 4080, 5440, 0,  0,  0,  0,  33,  0x61, 0, 0,  "Sinar",     "",       68},
            {1409024,  1376, 1024, 0,  0,  1,  0,  0,   0x49, 0, 0,  "Sony",      "XCD-SX910CR"},
            {2818048,  1376, 1024, 0,  0,  1,  0,  97,  0x49, 0, 0,  "Sony",      "XCD-SX910CR"},
            {17496000, 4320, 3240, 0,  0,  0,  0,  224, 0x94, 0, 0,  "Xiro",      "Xplorer V"},
    };
    static const char *corp[] =
            {"AgfaPhoto", "Canon", "Casio", "Epson", "Fujifilm",
             "Mamiya", "Minolta", "Motorola", "Kodak", "Konica", "Leica",
             "Nikon", "Nokia", "Olympus", "Ricoh", "Pentax", "Phase One",
             "Samsung", "Sigma", "Sinar", "Sony", "YI"};
    char head[32];
    char *cp;
    int hlen;
    int flen;
    int fsize;
    int zero_fsize = 1;
    int i;
    int c;
    struct jhead jh;

    cameraFlip = GLOBAL_flipsMask = IMAGE_filters = UINT_MAX; // unknown
    THE_image.height = THE_image.width = fuji_width = fuji_layout = cr2_slice[0] = 0;
    ADOBE_maximum = height = width = top_margin = left_margin = 0;
    GLOBAL_bayerPatternLabels[0] = desc[0] = artist[0] = GLOBAL_make[0] = GLOBAL_model[0] = model2[0] = 0;
    iso_speed = CAMERA_IMAGE_information.shutterSpeed = aperture = focal_len = unique_id = 0;
    numberOfRawImages = 0;
    memset(ifdArray, 0, sizeof ifdArray);
    memset(gpsdata, 0, sizeof gpsdata);
    memset(cblack, 0, sizeof cblack);
    memset(white, 0, sizeof white);
    memset(mask, 0, sizeof mask);
    thumb_offset = thumb_length = thumb_width = thumb_height = 0;
    TIFF_CALLBACK_loadRawData = CALLBACK_loadThumbnailRawData = 0;
    write_thumb = &jpeg_thumb;
    GLOBAL_IO_profileOffset = GLOBAL_meta_offset = meta_length = THE_image.bitsPerSample = tiff_compress = 0;
    kodak_cbpp = GLOBAL_IO_zeroAfterFf = GLOBAL_dngVersion = GLOBAL_loadFlags = 0;
    timestamp = shot_order = tiff_samples = ADOBE_black = is_foveon = 0;
    mix_green = profile_length = GLOBAL_IO_dataError = zero_is_bad = 0;
    pixel_aspect = is_raw = GLOBAL_colorTransformForRaw = 1;
    tile_width = tile_length = 0;

    for ( i = 0; i < 4; i++ ) {
        GLOBAL_cam_mul[i] = i == 1;
        pre_mul[i] = i < 3;
        for ( c = 0; c < 3; c++ ) {
            cmatrix[c][i] = 0;
        }
        for ( c = 0; c < 3; c++ ) {
            rgb_cam[c][i] = c == i;
        }
    }

    IMAGE_colors = 3;
    for ( i = 0; i < 0x10000; i++ ) {
        GAMMA_curveFunctionLookupTable[i] = i;
    }

    GLOBAL_endianOrder = read2bytes();
    hlen = read4bytes();
    fseek(GLOBAL_IO_ifp, 0, SEEK_SET);
    fread(head, 1, 32, GLOBAL_IO_ifp);
    fseek(GLOBAL_IO_ifp, 0, SEEK_END);
    flen = fsize = ftell(GLOBAL_IO_ifp);

    if ( (cp = (char *) memmem(head, 32, (char *) "MMMM", 4)) ||
         (cp = (char *) memmem(head, 32, (char *) "IIII", 4)) ) {
        parse_phase_one(cp - head);
        if ( cp - head && parse_tiff(0)) {
            apply_tiff();
        }
    } else if ( GLOBAL_endianOrder == LITTLE_ENDIAN_ORDER || GLOBAL_endianOrder == BIG_ENDIAN_ORDER ) {
        if ( !memcmp(head + 6, "HEAPCCDR", 8) ) {
            GLOBAL_IO_profileOffset = hlen;
            parse_ciff(hlen, flen - hlen, 0);
            TIFF_CALLBACK_loadRawData = &canon_load_raw;
        } else if ( parse_tiff(0) ) {
            apply_tiff();
        }
    } else if ( !memcmp(head, "\xff\xd8\xff\xe1", 4) && !memcmp(head + 6, "Exif", 4) ) {
        fseek(GLOBAL_IO_ifp, 4, SEEK_SET);
                GLOBAL_IO_profileOffset = 4 + read2bytes();
        fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset, SEEK_SET);
        if ( fgetc(GLOBAL_IO_ifp) != 0xff ) {
            parse_tiff(12);
        }
        thumb_offset = 0;
    } else if ( !memcmp(head + 25, "ARECOYK", 7) ) {
        strcpy(GLOBAL_make, "Contax");
        strcpy(GLOBAL_model, "N Digital");
        fseek(GLOBAL_IO_ifp, 33, SEEK_SET);
        get_timestamp(1);
        fseek(GLOBAL_IO_ifp, 60, SEEK_SET);
        for ( c = 0; c < 4; c++ ) {
            GLOBAL_cam_mul[c ^ (c >> 1)] = read4bytes();
        }
    } else if ( !strcmp(head, "PXN") ) {
        strcpy(GLOBAL_make, "Logitech");
        strcpy(GLOBAL_model, "Fotoman Pixtura");
    } else if ( !strcmp(head, "qktk") ) {
        strcpy(GLOBAL_make, "Apple");
        strcpy(GLOBAL_model, "QuickTake 100");
        TIFF_CALLBACK_loadRawData = &quicktake_100_load_raw;
    } else if ( !strcmp(head, "qktn") ) {
        strcpy(GLOBAL_make, "Apple");
        strcpy(GLOBAL_model, "QuickTake 150");
        TIFF_CALLBACK_loadRawData = &kodak_radc_load_raw;
    } else if ( !memcmp(head, "FUJIFILM", 8) ) {
        fseek(GLOBAL_IO_ifp, 84, SEEK_SET);
        thumb_offset = read4bytes();
        thumb_length = read4bytes();
        fseek(GLOBAL_IO_ifp, 92, SEEK_SET);
        parse_fuji(read4bytes());
        if ( thumb_offset > 120 ) {
            fseek(GLOBAL_IO_ifp, 120, SEEK_SET);
            i = read4bytes();
            if ( i ) {
                is_raw++;
            }
            if ( is_raw == 2 && OPTIONS_values->shotSelect ) {
                parse_fuji(i);
            }
        }
        fseek(GLOBAL_IO_ifp, 100 + 28 * (OPTIONS_values->shotSelect > 0), SEEK_SET);
        parse_tiff(GLOBAL_IO_profileOffset = read4bytes());
        parse_tiff(thumb_offset + 12);
        apply_tiff();
        if ( !TIFF_CALLBACK_loadRawData ) {
            TIFF_CALLBACK_loadRawData = &unpacked_load_raw;
            THE_image.bitsPerSample = 14;
        }
    } else if ( !memcmp(head, "RIFF", 4) ) {
        fseek(GLOBAL_IO_ifp, 0, SEEK_SET);
        parse_riff();
    } else if ( !memcmp(head + 4, "ftypcrx ", 8) ) {
        fseek(GLOBAL_IO_ifp, 0, SEEK_SET);
        parse_crx(fsize);
    } else if ( !memcmp(head + 4, "ftypqt   ", 9) ) {
        fseek(GLOBAL_IO_ifp, 0, SEEK_SET);
        parse_qt(fsize);
        is_raw = 0;
    } else if ( !memcmp(head, "\0\001\0\001\0@", 6) ) {
        fseek(GLOBAL_IO_ifp, 6, SEEK_SET);
        fread(GLOBAL_make, 1, 8, GLOBAL_IO_ifp);
        fread(GLOBAL_model, 1, 8, GLOBAL_IO_ifp);
        fread(model2, 1, 16, GLOBAL_IO_ifp);
                                                    GLOBAL_IO_profileOffset = read2bytes();
                                                    read2bytes();
                                                    THE_image.width = read2bytes();
                                                    THE_image.height = read2bytes();
        TIFF_CALLBACK_loadRawData = &nokia_load_raw;
                                                    IMAGE_filters = 0x61616161;
    } else if ( !memcmp(head, "NOKIARAW", 8) ) {
        strcpy(GLOBAL_make, "NOKIA");
        GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
        fseek(GLOBAL_IO_ifp, 300, SEEK_SET);
                                                        GLOBAL_IO_profileOffset = read4bytes();
        i = read4bytes();
        width = read2bytes();
        height = read2bytes();
        switch ( THE_image.bitsPerSample = i * 8 / (width * height) ) {
            case 8:
                TIFF_CALLBACK_loadRawData = &eight_bit_load_raw;
                break;
            case 10:
                TIFF_CALLBACK_loadRawData = &nokia_load_raw;
        }
                                                        THE_image.height = height + (top_margin = i / (width * THE_image.bitsPerSample / 8) - height);
        mask[0][3] = 1;
                                                        IMAGE_filters = 0x61616161;
    } else if ( !memcmp(head, "ARRI", 4) ) {
        GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
        fseek(GLOBAL_IO_ifp, 20, SEEK_SET);
        width = read4bytes();
        height = read4bytes();
        strcpy(GLOBAL_make, "ARRI");
        fseek(GLOBAL_IO_ifp, 668, SEEK_SET);
        fread(GLOBAL_model, 1, 64, GLOBAL_IO_ifp);
                                                            GLOBAL_IO_profileOffset = 4096;
        TIFF_CALLBACK_loadRawData = &packed_load_raw;
        GLOBAL_loadFlags = 88;
                                                            IMAGE_filters = 0x61616161;
    } else if ( !memcmp(head, "XPDS", 4) ) {
        GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
        fseek(GLOBAL_IO_ifp, 0x800, SEEK_SET);
        fread(GLOBAL_make, 1, 41, GLOBAL_IO_ifp);
                                                                THE_image.height = read2bytes();
                                                                THE_image.width = read2bytes();
        fseek(GLOBAL_IO_ifp, 56, SEEK_CUR);
        fread(GLOBAL_model, 1, 30, GLOBAL_IO_ifp);
                                                                GLOBAL_IO_profileOffset = 0x10000;
        TIFF_CALLBACK_loadRawData = &canon_rmf_load_raw;
        gamma_curve(0, 12.25, 1, 1023);
    } else if ( !memcmp(head + 4, "RED1", 4) ) {
        strcpy(GLOBAL_make, "Red");
        strcpy(GLOBAL_model, "One");
        parse_redcine();
        TIFF_CALLBACK_loadRawData = &redcine_load_raw;
        gamma_curve(1 / 2.4, 12.92, 1, 4095);
                                                                    IMAGE_filters = 0x49494949;
    } else if ( !memcmp(head, "DSC-Image", 9) ) {
        parse_rollei();
    } else if ( !memcmp(head, "PWAD", 4) ) {
        parse_sinar_ia();
    } else if ( !memcmp(head, "\0MRM", 4) ) {
        parse_minolta(0);
    } else if ( !memcmp(head, "FOVb", 4) ) {
        parse_foveon();
    } else if ( !memcmp(head, "CI", 2) ) {
        parse_cine();
    }

    if ( GLOBAL_make[0] == 0 ) {
        for ( zero_fsize = i = 0; i < sizeof table / sizeof *table; i++ ) {
            if ( fsize == table[i].fsize ) {
                strcpy(GLOBAL_make, table[i].make);
                strcpy(GLOBAL_model, table[i].model);
                GLOBAL_flipsMask = table[i].flags >> 2;
                zero_is_bad = table[i].flags & 2;
                if ( table[i].flags & 1 ) {
                    parse_external_jpeg();
                }
                GLOBAL_IO_profileOffset = table[i].offset;
                THE_image.width = table[i].rw;
                THE_image.height = table[i].rh;
                left_margin = table[i].lm;
                top_margin = table[i].tm;
                width = THE_image.width - left_margin - table[i].rm;
                height = THE_image.height - top_margin - table[i].bm;
                IMAGE_filters = 0x1010101 * table[i].cf;
                IMAGE_colors = 4 - !((IMAGE_filters & IMAGE_filters >> 1) & 0x5555);
                GLOBAL_loadFlags = table[i].lf;
                switch ( THE_image.bitsPerSample = (fsize - GLOBAL_IO_profileOffset) * 8 / (THE_image.width * THE_image.height) ) {
                    case 6:
                        TIFF_CALLBACK_loadRawData = &minolta_rd175_load_raw;
                        break;
                    case 8:
                        TIFF_CALLBACK_loadRawData = &eight_bit_load_raw;
                        break;
                    case 10:
                    case 12:
                        GLOBAL_loadFlags |= 512;
                        if ( !strcmp(GLOBAL_make, "Canon")) GLOBAL_loadFlags |= 256;
                        TIFF_CALLBACK_loadRawData = &packed_load_raw;
                        break;
                    case 16:
                        GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER | 0x404 * (GLOBAL_loadFlags & 1);
                        THE_image.bitsPerSample -= GLOBAL_loadFlags >> 4;
                        THE_image.bitsPerSample -= GLOBAL_loadFlags = GLOBAL_loadFlags >> 1 & 7;
                        TIFF_CALLBACK_loadRawData = &unpacked_load_raw;
                }
                ADOBE_maximum = (1 << THE_image.bitsPerSample) - (1 << table[i].max);
            }
        }
    }

    if ( zero_fsize ) {
        fsize = 0;
    }

    if ( GLOBAL_make[0] == 0 ) {
        parse_smal(0, flen);
    }

    if ( GLOBAL_make[0] == 0 ) {
        parse_jpeg(0);
        if ( !(strncmp(GLOBAL_model, "ov", 2) && strncmp(GLOBAL_model, "RP_OV", 5)) &&
             !fseek(GLOBAL_IO_ifp, -6404096, SEEK_END) &&
             fread(head, 1, 32, GLOBAL_IO_ifp) && !strcmp(head, "BRCMn")) {
            strcpy(GLOBAL_make, "OmniVision");
            GLOBAL_IO_profileOffset = ftell(GLOBAL_IO_ifp) + 0x8000 - 32;
            width = THE_image.width;
            THE_image.width = 2611;
            TIFF_CALLBACK_loadRawData = &nokia_load_raw;
            IMAGE_filters = 0x16161616;
        } else {
            is_raw = 0;
        }
    }

    for ( i = 0; i < sizeof corp / sizeof *corp; i++ ) {
        if ( strcasestr(GLOBAL_make, corp[i])) {
            // Simplify company names
            strcpy(GLOBAL_make, corp[i]);
        }
    }

    if ((!strcmp(GLOBAL_make, "Kodak") || !strcmp(GLOBAL_make, "Leica")) &&
        ((cp = strcasestr(GLOBAL_model, " DIGITAL CAMERA")) ||
         (cp = strstr(GLOBAL_model, "FILE VERSION"))))
        *cp = 0;

    if ( !strncasecmp(GLOBAL_model, "PENTAX", 6))
        strcpy(GLOBAL_make, "Pentax");

    cp = GLOBAL_make + strlen(GLOBAL_make);        /* Remove trailing spaces */
    while ( *--cp == ' ' ) *cp = 0;

    cp = GLOBAL_model + strlen(GLOBAL_model);
    while ( *--cp == ' ' ) *cp = 0;

    i = strlen(GLOBAL_make);            /* Remove make from model */
    if ( !strncasecmp(GLOBAL_model, GLOBAL_make, i) && GLOBAL_model[i++] == ' ' )
        memmove(GLOBAL_model, GLOBAL_model + i, 64 - i);

    if ( !strncmp(GLOBAL_model, "FinePix ", 8))
        strcpy(GLOBAL_model, GLOBAL_model + 8);

    if ( !strncmp(GLOBAL_model, "Digital Camera ", 15))
        strcpy(GLOBAL_model, GLOBAL_model + 15);
    desc[511] = artist[63] = GLOBAL_make[63] = GLOBAL_model[63] = model2[63] = 0;

    if ( !is_raw ) goto notraw;

    if ( !height ) height = THE_image.height;

    if ( !width ) width = THE_image.width;

    if ( height == 2624 && width == 3936 )    /* Pentax K10D and Samsung GX10 */
    {
        height = 2616;
        width = 3896;
    }

    if ( height == 3136 && width == 4864 )  /* Pentax K20D and Samsung GX20 */
    {
        height = 3124;
        width = 4688;
        IMAGE_filters = 0x16161616;
    }

    if ( THE_image.height == 2868 && (!strcmp(GLOBAL_model, "K-r") || !strcmp(GLOBAL_model, "K-x"))) {
        width = 4309;
        IMAGE_filters = 0x16161616;
    }

    if ( THE_image.height == 3136 && !strcmp(GLOBAL_model, "K-7")) {
        height = 3122;
        width = 4684;
        IMAGE_filters = 0x16161616;
        top_margin = 2;
    }

    if ( THE_image.height == 3284 && !strncmp(GLOBAL_model, "K-5", 3)) {
        left_margin = 10;
        width = 4950;
        IMAGE_filters = 0x16161616;
    }

    if ( THE_image.height == 3300 && !strncmp(GLOBAL_model, "K-50", 4)) {
        height = 3288, width = 4952;
        left_margin = 0;
        top_margin = 12;
    }

    if ( THE_image.height == 3664 && !strncmp(GLOBAL_model, "K-S", 3)) {
        width = 5492;
        left_margin = 0;
    }

    if ( THE_image.height == 4032 && !strcmp(GLOBAL_model, "K-3")) {
        height = 4032;
        width = 6040;
        left_margin = 4;
    }

    if ( THE_image.height == 4060 && !strcmp(GLOBAL_model, "KP")) {
        height = 4032;
        width = 6032;
        left_margin = 52;
        top_margin = 28;
    }

    if ( THE_image.height == 4950 && !strcmp(GLOBAL_model, "K-1")) {
        height = 4932;
        width = 7380;
        left_margin = 4;
        top_margin = 18;
    }

    if ( THE_image.height == 5552 && !strcmp(GLOBAL_model, "645D")) {
        height = 5502;
        width = 7328;
        left_margin = 48;
        top_margin = 29;
        IMAGE_filters = 0x61616161;
    }

    if ( height == 3014 && width == 4096 )    /* Ricoh GX200 */
        width = 4014;

    if ( GLOBAL_dngVersion ) {
        if ( IMAGE_filters == UINT_MAX) IMAGE_filters = 0;
        if ( IMAGE_filters ) is_raw *= tiff_samples;
        else IMAGE_colors = tiff_samples;
        switch ( tiff_compress ) {
            case 0:
            case 1:
                TIFF_CALLBACK_loadRawData = &packed_dng_load_raw;
                break;
            case 7:
                TIFF_CALLBACK_loadRawData = &lossless_dng_load_raw;
                break;
            case 34892:
                TIFF_CALLBACK_loadRawData = &lossy_dng_load_raw;
                break;
            default:
                TIFF_CALLBACK_loadRawData = 0;
        }
        goto dng_skip;
    }

    if ( !strcmp(GLOBAL_make, "Canon") && !fsize && THE_image.bitsPerSample != 15 ) {
        if ( !TIFF_CALLBACK_loadRawData )
            TIFF_CALLBACK_loadRawData = &lossless_jpeg_load_raw;
        for ( i = 0; i < sizeof canon / sizeof *canon; i++ )
            if ( THE_image.width == canon[i][0] && THE_image.height == canon[i][1] ) {
                width = THE_image.width - (left_margin = canon[i][2]);
                height = THE_image.height - (top_margin = canon[i][3]);
                width -= canon[i][4];
                height -= canon[i][5];
                mask[0][1] = canon[i][6];
                mask[0][3] = -canon[i][7];
                mask[1][1] = canon[i][8];
                mask[1][3] = -canon[i][9];
                if ( canon[i][10] ) IMAGE_filters = canon[i][10] * 0x01010101;
            }
        if ((unique_id | 0x20000) == 0x2720000 ) {
            left_margin = 8;
            top_margin = 16;
        }
    }

    for ( i = 0; i < sizeof unique / sizeof *unique; i++ )
        if ( unique_id == 0x80000000 + unique[i].id ) {
            adobe_coeff("Canon", unique[i].model);
            if ( GLOBAL_model[4] == 'K' && strlen(GLOBAL_model) == 8 )
                strcpy(GLOBAL_model, unique[i].model);
        }

    for ( i = 0; i < sizeof sonique / sizeof *sonique; i++ )
        if ( unique_id == sonique[i].id )
            strcpy(GLOBAL_model, sonique[i].model);

    for ( i = 0; i < sizeof panalias / sizeof *panalias; i++ )
        if ( panalias[i][0] == '@' ) orig = panalias[i] + 1;
        else
            if ( !strcmp(GLOBAL_model, panalias[i]))
                adobe_coeff("Panasonic", orig);

    if ( !strcmp(GLOBAL_make, "Nikon")) {
        if ( !TIFF_CALLBACK_loadRawData )
            TIFF_CALLBACK_loadRawData = &packed_load_raw;
        if ( GLOBAL_model[0] == 'E' )
            GLOBAL_loadFlags |= !GLOBAL_IO_profileOffset << 2 | 2;
    }

    // Set parameters based on camera name (for non-DNG files)

    if ( !strcmp(GLOBAL_model, "KAI-0340")
         && find_green(16, 16, 3840, 5120) < 25 ) {
        height = 480;
        top_margin = IMAGE_filters = 0;
        strcpy(GLOBAL_model, "C603");
    }

    if ( !strcmp(GLOBAL_make, "Sony") && THE_image.width > 3888 )
        ADOBE_black = 128 << (THE_image.bitsPerSample - 12);

    //-------------------------------------------------------------------------------------------------------------
    if ( is_foveon ) {
        if ( height * 2 < width ) pixel_aspect = 0.5;
        if ( height > width ) pixel_aspect = 2;
        IMAGE_filters = 0;
        simple_coeff(0);
    } else if ( !strcmp(GLOBAL_make, "Canon") && THE_image.bitsPerSample == 15 ) {
        switch ( width ) {
        case 3344:
        width -= 66;
        case 3872:
        width -= 6;
        }
        if ( height > width ) {
        SWAP(height, width);
        SWAP(THE_image.height, THE_image.width);
        }
        if ( width == 7200 && height == 3888 ) {
            THE_image.width = width = 6480;
            THE_image.height = height = 4320;
        }
            IMAGE_filters = 0;
            tiff_samples = IMAGE_colors = 3;
        TIFF_CALLBACK_loadRawData = &canon_sraw_load_raw;
    } else if ( !strcmp(GLOBAL_model, "PowerShot 600") ) {
        height = 613;
        width = 854;
                THE_image.width = 896;
                IMAGE_colors = 4;
                IMAGE_filters = 0xe1e4e1e4;
        TIFF_CALLBACK_loadRawData = &canon_600_load_raw;
    } else if ( !strcmp(GLOBAL_model, "PowerShot A5") ||
                !strcmp(GLOBAL_model, "PowerShot A5 Zoom") ) {
        height = 773;
        width = 960;
                    THE_image.width = 992;
        pixel_aspect = 256 / 235.0;
                    IMAGE_filters = 0x1e4e1e4e;
        goto canon_a5;
    } else if ( !strcmp(GLOBAL_model, "PowerShot A50") ) {
        height = 968;
        width = 1290;
                        THE_image.width = 1320;
                        IMAGE_filters = 0x1b4e4b1e;
        goto canon_a5;
    } else if ( !strcmp(GLOBAL_model, "PowerShot Pro70") ) {
        height = 1024;
        width = 1552;
                            IMAGE_filters = 0x1e4b4e1b;
        canon_a5:
                            IMAGE_colors = 4;
                            THE_image.bitsPerSample = 10;
        TIFF_CALLBACK_loadRawData = &packed_load_raw;
        GLOBAL_loadFlags = 264;
    } else if ( !strcmp(GLOBAL_model, "PowerShot Pro90 IS") ||
                !strcmp(GLOBAL_model, "PowerShot G1")) {
                                IMAGE_colors = 4;
                                IMAGE_filters = 0xb4b4b4b4;
    } else if ( !strcmp(GLOBAL_model, "PowerShot A610") ) {
        if ( canon_s2is() ) {
            strcpy(GLOBAL_model + 10, "S2 IS");
        }
    } else if ( !strcmp(GLOBAL_model, "PowerShot SX220 HS") ) {
        mask[1][3] = -4;
    } else if ( !strcmp(GLOBAL_model, "EOS D2000C") ) {
                                            IMAGE_filters = 0x61616161;
                                            ADOBE_black = GAMMA_curveFunctionLookupTable[200];
    } else if ( !strcmp(GLOBAL_model, "EOS 80D") ) {
        top_margin -= 2;
        height += 2;
    } else if ( !strcmp(GLOBAL_model, "D1") ) {
                                                    GLOBAL_cam_mul[0] *= 256 / 527.0;
                                                    GLOBAL_cam_mul[2] *= 256 / 317.0;
    } else if ( !strcmp(GLOBAL_model, "D1X") ) {
        width -= 4;
        pixel_aspect = 0.5;
    } else if ( !strcmp(GLOBAL_model, "D40X") || !strcmp(GLOBAL_model, "D60") || !strcmp(GLOBAL_model, "D80") || !strcmp(GLOBAL_model, "D3000") ) {
        height -= 3;
        width -= 4;
    } else if ( !strcmp(GLOBAL_model, "D3") || !strcmp(GLOBAL_model, "D3S") || !strcmp(GLOBAL_model, "D700") ) {
        width -= 4;
        left_margin = 2;
    } else if ( !strcmp(GLOBAL_model, "D3100") ) {
        width -= 28;
        left_margin = 6;
    } else if ( !strcmp(GLOBAL_model, "D5000") || !strcmp(GLOBAL_model, "D90") ) {
        width -= 42;
    } else if ( !strcmp(GLOBAL_model, "D5100") || !strcmp(GLOBAL_model, "D7000") || !strcmp(GLOBAL_model, "COOLPIX A") ) {
        width -= 44;
    } else if ( !strcmp(GLOBAL_model, "D3200") || !strncmp(GLOBAL_model, "D6", 2) || !strncmp(GLOBAL_model, "D800", 4) ) {
        width -= 46;
    } else if ( !strcmp(GLOBAL_model, "D4") || !strcmp(GLOBAL_model, "Df") ) {
        width -= 52;
        left_margin = 2;
    } else if ( !strncmp(GLOBAL_model, "D40", 3) || !strncmp(GLOBAL_model, "D50", 3) || !strncmp(GLOBAL_model, "D70", 3) ) {
       width--;
    } else if ( !strcmp(GLOBAL_model, "D100") ) {
    if ( GLOBAL_loadFlags )
        THE_image.width = (width += 3) + 3;
    } else if ( !strcmp(GLOBAL_model, "D200") ) {
        left_margin = 1;
        width -= 4;
                                                                                                IMAGE_filters = 0x94949494;
    } else if ( !strncmp(GLOBAL_model, "D2H", 3) ) {
        left_margin = 6;
        width -= 14;
    } else if ( !strncmp(GLOBAL_model, "D2X", 3) ) {
        if ( width == 3264 ) {
            width -= 32;
        } else {
            width -= 8;
        }
    } else if ( !strncmp(GLOBAL_model, "D300", 4) ) {
        width -= 32;
    } else if ( !strncmp(GLOBAL_model, "COOLPIX B", 9) ) {
        GLOBAL_loadFlags = 24;
    } else if ( !strncmp(GLOBAL_model, "COOLPIX P", 9) && THE_image.width != 4032 ) {
        GLOBAL_loadFlags = 24;
                                                                                                                    IMAGE_filters = 0x94949494;
        if ( GLOBAL_model[9] == '7' && iso_speed >= 400 ) {
            ADOBE_black = 255;
        }
    } else if ( !strncmp(GLOBAL_model, "1 ", 2) ) {
        height -= 2;
    } else if ( fsize == 1581060 ) {
        simple_coeff(3);
        pre_mul[0] = 1.2085;
        pre_mul[1] = 1.0943;
        pre_mul[3] = 1.1103;
    } else if ( fsize == 3178560 ) {
                                                                                                                                GLOBAL_cam_mul[0] *= 4;
                                                                                                                                GLOBAL_cam_mul[2] *= 4;
    } else if ( fsize == 4771840 ) {
        if ( !timestamp && nikon_e995() ) {
            strcpy(GLOBAL_model, "E995");
        }
        if ( strcmp(GLOBAL_model, "E995") ) {
            IMAGE_filters = 0xb4b4b4b4;
            simple_coeff(3);
            pre_mul[0] = 1.196;
            pre_mul[1] = 1.246;
            pre_mul[2] = 1.018;
        }
    } else if ( fsize == 2940928 ) {
        if ( !timestamp && !nikon_e2100() ) {
            strcpy(GLOBAL_model, "E2500");
        }
        if ( !strcmp(GLOBAL_model, "E2500") ) {
            height -= 2;
            GLOBAL_loadFlags = 6;
            IMAGE_colors = 4;
            IMAGE_filters = 0x4b4b4b4b;
        }
    } else if ( fsize == 4775936 ) {
        if ( !timestamp ) {
            nikon_3700();
        }
        if ( GLOBAL_model[0] == 'E' && atoi(GLOBAL_model + 1) < 3700 ) {
            IMAGE_filters = 0x49494949;
        }
        if ( !strcmp(GLOBAL_model, "Optio 33WR") ) {
            GLOBAL_flipsMask = 1;
            IMAGE_filters = 0x16161616;
        }
        if ( GLOBAL_make[0] == 'O' ) {
            i = find_green(12, 32, 1188864, 3576832);
            c = find_green(12, 32, 2383920, 2387016);
            if ( abs(i) < abs(c)) {
                SWAP(i, c);
                GLOBAL_loadFlags = 24;
            }
            if ( i < 0 ) {
                IMAGE_filters = 0x61616161;
            }
        }
    } else if ( fsize == 5869568 ) {
        if ( !timestamp && minolta_z2() ) {
            strcpy(GLOBAL_make, "Minolta");
            strcpy(GLOBAL_model, "DiMAGE Z2");
        }
        GLOBAL_loadFlags = 6 + 24 * (GLOBAL_make[0] == 'M');
    } else if ( fsize == 6291456 ) {
        fseek(GLOBAL_IO_ifp, 0x300000, SEEK_SET);
        if ( (GLOBAL_endianOrder = guess_byte_order(0x10000)) == BIG_ENDIAN_ORDER ) {
            height -= (top_margin = 16);
            width -= (left_margin = 28);
            ADOBE_maximum = 0xf5c0;
            strcpy(GLOBAL_make, "ISG");
            GLOBAL_model[0] = 0;
        }
    } else if ( !strcmp(GLOBAL_make, "Fujifilm") ) {
        if ( !strcmp(GLOBAL_model + 7, "S2Pro") ) {
            strcpy(GLOBAL_model, "S2Pro");
            height = 2144;
            width = 2880;
            GLOBAL_flipsMask = 6;
        }
        top_margin = (THE_image.height - height) >> 2 << 1;
        left_margin = (THE_image.width - width) >> 2 << 1;
        if ( width == 2848 || width == 3664 ) {
            IMAGE_filters = 0x16161616;
        }
        if ( width == 4032 || width == 4952 || width == 6032 || width == 8280 ) {
            left_margin = 0;
        }
        if ( width == 3328 && (width -= 66) ) {
            left_margin = 34;
        }
        if ( width == 4936 ) {
            left_margin = 4;
        }
        if ( !strcmp(GLOBAL_model, "HS50EXR") || !strcmp(GLOBAL_model, "F900EXR") ) {
            width += 2;
            left_margin = 0;
            IMAGE_filters = 0x16161616;
        }
        if ( fuji_layout ) {
            THE_image.width *= is_raw;
        }
        if ( IMAGE_filters == 9 ) {
            for ( c = 0; c < 36; c++ ) {
                ((char *) xtrans)[c] = xtrans_abs[(c / 6 + top_margin) % 6][(c + left_margin) % 6];
            }
        }
    } else if ( !strcmp(GLOBAL_model, "KD-400Z") ) {
        height = 1712;
        width = 2312;
                                                                                                                                                            THE_image.width = 2336;
        goto konica_400z;
    } else if ( !strcmp(GLOBAL_model, "KD-510Z") ) {
        goto konica_510z;
    } else if ( !strcasecmp(GLOBAL_make, "Minolta") ) {
        if ( !TIFF_CALLBACK_loadRawData && (ADOBE_maximum = 0xfff) ) {
            TIFF_CALLBACK_loadRawData = &unpacked_load_raw;
        }
        if ( !strncmp(GLOBAL_model, "DiMAGE A", 8) ) {
            if ( !strcmp(GLOBAL_model, "DiMAGE A200") ) {
                IMAGE_filters = 0x49494949;
            }
            THE_image.bitsPerSample = 12;
            TIFF_CALLBACK_loadRawData = &packed_load_raw;
        } else if ( !strncmp(GLOBAL_model, "ALPHA", 5) || !strncmp(GLOBAL_model, "DYNAX", 5) || !strncmp(GLOBAL_model, "MAXXUM", 6) ) {
            snprintf(GLOBAL_model + 20, 40, "DYNAX %-10s", GLOBAL_model + 6 + (GLOBAL_model[0] == 'M'));
            adobe_coeff(GLOBAL_make, GLOBAL_model + 20);
            TIFF_CALLBACK_loadRawData = &packed_load_raw;
        } else if ( !strncmp(GLOBAL_model, "DiMAGE G", 8) ) {
            if ( GLOBAL_model[8] == '4' ) {
                height = 1716;
                width = 2304;
            } else if ( GLOBAL_model[8] == '5' ) {
                konica_510z:
                height = 1956;
                width = 2607;
                    THE_image.width = 2624;
            } else if ( GLOBAL_model[8] == '6' ) {
                height = 2136;
                width = 2848;
            }
                    GLOBAL_IO_profileOffset += 14;
                    IMAGE_filters = 0x61616161;
            konica_400z:
            TIFF_CALLBACK_loadRawData = &unpacked_load_raw;
                    ADOBE_maximum = 0x3df;
            GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
        }
    } else if ( !strcmp(GLOBAL_model, "*ist D") ) {
        TIFF_CALLBACK_loadRawData = &unpacked_load_raw;
                                                                                                                                                                        GLOBAL_IO_dataError = -1;
    } else if ( !strcmp(GLOBAL_model, "*ist DS") ) {
        height -= 2;
    } else if ( !strcmp(GLOBAL_make, "Samsung") && THE_image.width == 4704 ) {
        height -= top_margin = 8;
        width -= 2 * (left_margin = 8);
        GLOBAL_loadFlags = 256;
    } else if ( !strcmp(GLOBAL_make, "Samsung") && THE_image.height == 3714 ) {
        height -= top_margin = 18;
        left_margin = THE_image.width - (width = 5536);
        if ( THE_image.width != 5600 ) {
            left_margin = top_margin = 0;
        }
                                                                                                                                                                                    IMAGE_filters = 0x61616161;
                                                                                                                                                                                    IMAGE_colors = 3;
    } else if ( !strcmp(GLOBAL_make, "Samsung") && THE_image.width == 5632 ) {
        GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
        height = 3694;
        top_margin = 2;
        width = 5574 - (left_margin = 32 + THE_image.bitsPerSample);
        if ( THE_image.bitsPerSample == 12 ) {
            GLOBAL_loadFlags = 80;
        }
    } else if ( !strcmp(GLOBAL_make, "Samsung") && THE_image.width == 5664 ) {
        height -= top_margin = 17;
        left_margin = 96;
        width = 5544;
                                                                                                                                                                                            IMAGE_filters = 0x49494949;
    } else if ( !strcmp(GLOBAL_make, "Samsung") && THE_image.width == 6496 ) {
                                                                                                                                                                                                IMAGE_filters = 0x61616161;
                                                                                                                                                                                                ADOBE_black = 1 << (THE_image.bitsPerSample - 7);
    } else if ( !strcmp(GLOBAL_model, "EX1") ) {
        GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
        height -= 20;
        top_margin = 2;
        if ( (width -= 6) > 3682 ) {
            height -= 10;
            width -= 46;
            top_margin = 8;
        }
    } else if ( !strcmp(GLOBAL_model, "WB2000") ) {
        GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
        height -= 3;
        top_margin = 2;
        if ( (width -= 10) > 3718 ) {
            height -= 28;
            width -= 56;
            top_margin = 8;
        }
    } else if ( strstr(GLOBAL_model, "WB550") ) {
        strcpy(GLOBAL_model, "WB550");
    } else if ( !strcmp(GLOBAL_model, "EX2F") ) {
        height = 3045;
        width = 4070;
        top_margin = 3;
        GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
                                                                                                                                                                                                                IMAGE_filters = 0x49494949;
        TIFF_CALLBACK_loadRawData = &unpacked_load_raw;
    } else if ( !strcmp(GLOBAL_model, "STV680 VGA") ) {
                                                                                                                                                                                                                    ADOBE_black = 16;
    } else if ( !strcmp(GLOBAL_model, "N95") ) {
        height = THE_image.height - (top_margin = 2);
    } else if ( !strcmp(GLOBAL_model, "640x480") ) {
        gamma_curve(0.45, 4.5, 1, 255);
    } else if ( !strcmp(GLOBAL_make, "Hasselblad") ) {
        if ( TIFF_CALLBACK_loadRawData == &lossless_jpeg_load_raw ) {
            TIFF_CALLBACK_loadRawData = &hasselblad_load_raw;
        }
        if ( THE_image.width == 7262 ) {
            height = 5444;
            width = 7248;
            top_margin = 4;
            left_margin = 7;
            IMAGE_filters = 0x61616161;
        } else if ( THE_image.width == 7410 || THE_image.width == 8282 ) {
            height -= 84;
            width -= 82;
            top_margin = 4;
            left_margin = 41;
                IMAGE_filters = 0x61616161;
        } else if ( THE_image.width == 8384 ) {
            height = 6208;
            width = 8280;
            top_margin = 96;
            left_margin = 46;
        } else if ( THE_image.width == 9044 ) {
            height = 6716;
            width = 8964;
            top_margin = 8;
            left_margin = 40;
                        ADOBE_black += GLOBAL_loadFlags = 256;
                        ADOBE_maximum = 0x8101;
        } else if ( THE_image.width == 4090 ) {
            strcpy(GLOBAL_model, "V96C");
            height -= (top_margin = 6);
            width -= (left_margin = 3) + 7;
                            IMAGE_filters = 0x61616161;
        }
        if ( tiff_samples > 1 ) {
            is_raw = tiff_samples + 1;
            if ( !OPTIONS_values->shotSelect && !OPTIONS_values->halfSizePreInterpolation ) {
                IMAGE_filters = 0;
            }
        }
    } else if ( !strcmp(GLOBAL_make, "Sinar") ) {
        if ( !TIFF_CALLBACK_loadRawData ) {
            TIFF_CALLBACK_loadRawData = &unpacked_load_raw;
        }
        if ( is_raw > 1 && !OPTIONS_values->shotSelect && !OPTIONS_values->halfSizePreInterpolation ) {
            IMAGE_filters = 0;
        }
                                                                                                                                                                                                                                    ADOBE_maximum = 0x3fff;
    } else if ( !strcmp(GLOBAL_make, "Leaf") ) {
                                                                                                                                                                                                                                        ADOBE_maximum = 0x3fff;
        fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset, SEEK_SET);
        if ( ljpeg_start(&jh, 1) && jh.bits == 15 ) {
            ADOBE_maximum = 0x1fff;
        }
        if ( tiff_samples > 1 ) {
            IMAGE_filters = 0;
        }
        if ( tiff_samples > 1 || tile_length < THE_image.height ) {
            TIFF_CALLBACK_loadRawData = &leaf_hdr_load_raw;
            THE_image.width = tile_width;
        }
        if ( (width | height) == 2048 ) {
            if ( tiff_samples == 1 ) {
                IMAGE_filters = 1;
                strcpy(GLOBAL_bayerPatternLabels, "RBTG");
                strcpy(GLOBAL_model, "CatchLight");
                top_margin = 8;
                left_margin = 18;
                height = 2032;
                width = 2016;
            } else {
                strcpy(GLOBAL_model, "DCB2");
                top_margin = 10;
                left_margin = 16;
                height = 2028;
                width = 2022;
            }
        } else if ( width + height == 3144 + 2060 ) {
            if ( !GLOBAL_model[0] ) {
                strcpy(GLOBAL_model, "Cantare");
            }
            if ( width > height ) {
                top_margin = 6;
                left_margin = 32;
                height = 2048;
                width = 3072;
                IMAGE_filters = 0x61616161;
            } else {
                left_margin = 6;
                top_margin = 32;
                width = 2048;
                height = 3072;
                IMAGE_filters = 0x16161616;
            }
            if ( !GLOBAL_cam_mul[0] || GLOBAL_model[0] == 'V' ) {
                IMAGE_filters = 0;
            } else {
                is_raw = tiff_samples;
            }
        } else if ( width == 2116 ) {
            strcpy(GLOBAL_model, "Valeo 6");
            height -= 2 * (top_margin = 30);
            width -= 2 * (left_margin = 55);
                    IMAGE_filters = 0x49494949;
        } else if ( width == 3171 ) {
            strcpy(GLOBAL_model, "Valeo 6");
            height -= 2 * (top_margin = 24);
            width -= 2 * (left_margin = 24);
                        IMAGE_filters = 0x16161616;
        }
    } else if ( !strcmp(GLOBAL_make, "Leica") || !strcmp(GLOBAL_make, "Panasonic") ) {
        if ((flen - GLOBAL_IO_profileOffset) / (THE_image.width * 8 / 7) == THE_image.height ) {
            TIFF_CALLBACK_loadRawData = &panasonic_load_raw;
        }
        if ( !TIFF_CALLBACK_loadRawData ) {
            TIFF_CALLBACK_loadRawData = &unpacked_load_raw;
            GLOBAL_loadFlags = 4;
        }
        zero_is_bad = 1;
        if ((height += 12) > THE_image.height ) {
            height = THE_image.height;
        }
        for ( i = 0; i < sizeof pana / sizeof *pana; i++ ) {
            if ( THE_image.width == pana[i][0] && THE_image.height == pana[i][1] ) {
                left_margin = pana[i][2];
                top_margin = pana[i][3];
                width += pana[i][4];
                height += pana[i][5];
            }
        }
                                                                                                                                                                                                                                            IMAGE_filters = 0x01010101 * (unsigned char) "\x94\x61\x49\x16"[((IMAGE_filters - 1) ^ (left_margin & 1) ^ (top_margin << 1)) & 3];
    } else if ( !strcmp(GLOBAL_model, "C770UZ") ) {
        height = 1718;
        width = 2304;
                                                                                                                                                                                                                                                IMAGE_filters = 0x16161616;
        TIFF_CALLBACK_loadRawData = &packed_load_raw;
        GLOBAL_loadFlags = 30;
    } else if ( !strcmp(GLOBAL_make, "Olympus") ) {
        height += height & 1;
        if ( exif_cfa ) {
            IMAGE_filters = exif_cfa;
        }
        if ( width == 4100 ) {
            width -= 4;
        }
        if ( width == 4080 ) {
            width -= 24;
        }
        if ( width == 9280 ) {
            width -= 6;
            height -= 6;
        }
        if ( TIFF_CALLBACK_loadRawData == &unpacked_load_raw ) {
            GLOBAL_loadFlags = 4;
        }
                                                                                                                                                                                                                                                    THE_image.bitsPerSample = 12;
        if ( !strcmp(GLOBAL_model, "E-300") || !strcmp(GLOBAL_model, "E-500") ) {
            width -= 20;
            if ( TIFF_CALLBACK_loadRawData == &unpacked_load_raw ) {
                ADOBE_maximum = 0xfc3;
                memset(cblack, 0, sizeof cblack);
            }
        } else if ( !strcmp(GLOBAL_model, "E-330") ) {
            width -= 30;
            if ( TIFF_CALLBACK_loadRawData == &unpacked_load_raw ) {
                ADOBE_maximum = 0xf79;
            }
        } else if ( !strcmp(GLOBAL_model, "SP550UZ") ) {
            thumb_length = flen - (thumb_offset = 0xa39800);
            thumb_height = 480;
            thumb_width = 640;
        } else if ( !strcmp(GLOBAL_model, "TG-4") ) {
            width -= 16;
        } else if ( !strcmp(GLOBAL_model, "TG-5") ) {
            width -= 6;
        }
    } else if ( !strcmp(GLOBAL_model, "N Digital") ) {
        height = 2047;
        width = 3072;
                                                                                                                                                                                                                                                        IMAGE_filters = 0x61616161;
                                                                                                                                                                                                                                                        GLOBAL_IO_profileOffset = 0x1a00;
        TIFF_CALLBACK_loadRawData = &packed_load_raw;
    } else if ( !strcmp(GLOBAL_model, "DSC-F828") ) {
        width = 3288;
        left_margin = 5;
        mask[1][3] = -17;
                                                                                                                                                                                                                                                            GLOBAL_IO_profileOffset = 862144;
        TIFF_CALLBACK_loadRawData = &sony_load_raw;
                                                                                                                                                                                                                                                            IMAGE_filters = 0x9c9c9c9c;
                                                                                                                                                                                                                                                            IMAGE_colors = 4;
        strcpy(GLOBAL_bayerPatternLabels, "RGBE");
    } else if ( !strcmp(GLOBAL_model, "DSC-V3") ) {
        width = 3109;
        left_margin = 59;
        mask[0][1] = 9;
                                                                                                                                                                                                                                                                GLOBAL_IO_profileOffset = 787392;
        TIFF_CALLBACK_loadRawData = &sony_load_raw;
    } else if ( !strcmp(GLOBAL_make, "Sony") && THE_image.width == 3984 ) {
        width = 3925;
        GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
    } else if ( !strcmp(GLOBAL_make, "Sony") && THE_image.width == 4288 ) {
        width -= 32;
    } else if ( !strcmp(GLOBAL_make, "Sony") && THE_image.width == 4600 ) {
        if ( !strcmp(GLOBAL_model, "DSLR-A350") ) {
            height -= 4;
        }
                                                                                                                                                                                                                                                                            ADOBE_black = 0;
    } else if ( !strcmp(GLOBAL_make, "Sony") && THE_image.width == 4928 ) {
        if ( height < 3280 ) {
            width -= 8;
        }
    } else if ( !strcmp(GLOBAL_make, "Sony") && THE_image.width == 5504 ) {
        width -= height > 3664 ? 8 : 32;
        if ( !strncmp(GLOBAL_model, "DSC", 3) ) {
            ADOBE_black = 200 << (THE_image.bitsPerSample - 12);
        }
    } else if ( !strcmp(GLOBAL_make, "Sony") && THE_image.width == 6048 ) {
        width -= 24;
        if ( strstr(GLOBAL_model, "RX1") || strstr(GLOBAL_model, "A99") ) {
            width -= 6;
        }
    } else if ( !strcmp(GLOBAL_make, "Sony") && THE_image.width == 7392 ) {
        width -= 30;
    } else if ( !strcmp(GLOBAL_make, "Sony") && THE_image.width == 8000 ) {
        width -= 32;
    } else if ( !strcmp(GLOBAL_model, "DSLR-A100") ) {
        if ( width == 3880 ) {
            height--;
            width = ++THE_image.width;
        } else {
            height -= 4;
            width -= 4;
            GLOBAL_endianOrder = BIG_ENDIAN_ORDER;
            GLOBAL_loadFlags = 2;
        }
                                                                                                                                                                                                                                                                                                    IMAGE_filters = 0x61616161;
    } else if ( !strcmp(GLOBAL_model, "PIXL") ) {
        height -= top_margin = 4;
        width -= left_margin = 32;
        gamma_curve(0, 7, 1, 255);
    } else if ( !strcmp(GLOBAL_model, "C603") || !strcmp(GLOBAL_model, "C330") || !strcmp(GLOBAL_model, "12MP") ) {
        GLOBAL_endianOrder = LITTLE_ENDIAN_ORDER;
        if ( IMAGE_filters && GLOBAL_IO_profileOffset ) {
            fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset < 4096 ? 168 : 5252, SEEK_SET);
            readShorts(GAMMA_curveFunctionLookupTable, 256);
        } else {
            gamma_curve(0, 3.875, 1, 255);
        }
        TIFF_CALLBACK_loadRawData = IMAGE_filters ?
                                    &eight_bit_load_raw : strcmp(GLOBAL_model, "C330") ? &kodak_c603_load_raw : &kodak_c330_load_raw;
        GLOBAL_loadFlags = THE_image.bitsPerSample > 16;
                                                                                                                                                                                                                                                                                                            THE_image.bitsPerSample = 8;
    } else if ( !strncasecmp(GLOBAL_model, "EasyShare", 9) ) {
                                                                                                                                                                                                                                                                                                                GLOBAL_IO_profileOffset = GLOBAL_IO_profileOffset < 0x15000 ? 0x15000 : 0x17000;
        TIFF_CALLBACK_loadRawData = &packed_load_raw;
    } else if ( !strcasecmp(GLOBAL_make, "Kodak") ) {
        if ( IMAGE_filters == UINT_MAX ) {
            IMAGE_filters = 0x61616161;
        }
        if ( !strncmp(GLOBAL_model, "NC2000", 6) || !strncmp(GLOBAL_model, "EOSDCS", 6) ||
             !strncmp(GLOBAL_model, "DCS4", 4) ) {
            width -= 4;
            left_margin = 2;
            if ( GLOBAL_model[6] == ' ' ) {
                GLOBAL_model[6] = 0;
            }
            if ( !strcmp(GLOBAL_model, "DCS460A") ) {
                goto bw;
            }
        } else if ( !strcmp(GLOBAL_model, "DCS660M") ) {
                ADOBE_black = 214;
            goto bw;
        } else if ( !strcmp(GLOBAL_model, "DCS760M") ) {
            bw:
                    IMAGE_colors = 1;
                    IMAGE_filters = 0;
        }
        if ( !strcmp(GLOBAL_model + 4, "20X") ) {
            strcpy(GLOBAL_bayerPatternLabels, "MYCY");
        }
        if ( strstr(GLOBAL_model, "DC25") ) {
            strcpy(GLOBAL_model, "DC25");
            GLOBAL_IO_profileOffset = 15424;
        }
        if ( !strncmp(GLOBAL_model, "DC2", 3) ) {
            THE_image.height = 2 + (height = 242);
            if ( flen < 100000 ) {
                THE_image.width = 256;
                width = 249;
                pixel_aspect = (4.0 * height) / (3.0 * width);
            } else {
                THE_image.width = 512;
                width = 501;
                pixel_aspect = (493.0 * height) / (373.0 * width);
            }
            top_margin = left_margin = 1;
            IMAGE_colors = 4;
            IMAGE_filters = 0x8d8d8d8d;
            simple_coeff(1);
            pre_mul[1] = 1.179;
            pre_mul[2] = 1.209;
            pre_mul[3] = 1.036;
            TIFF_CALLBACK_loadRawData = &eight_bit_load_raw;
        } else if ( !strcmp(GLOBAL_model, "40") ) {
            strcpy(GLOBAL_model, "DC40");
            height = 512;
            width = 768;
                GLOBAL_IO_profileOffset = 1152;
            TIFF_CALLBACK_loadRawData = &kodak_radc_load_raw;
                THE_image.bitsPerSample = 12;
        } else if ( strstr(GLOBAL_model, "DC50") ) {
            strcpy(GLOBAL_model, "DC50");
            height = 512;
            width = 768;
                    GLOBAL_IO_profileOffset = 19712;
            TIFF_CALLBACK_loadRawData = &kodak_radc_load_raw;
        } else if ( strstr(GLOBAL_model, "DC120") ) {
            strcpy(GLOBAL_model, "DC120");
            height = 976;
            width = 848;
            pixel_aspect = height / 0.75 / width;
            TIFF_CALLBACK_loadRawData = tiff_compress == 7 ? &kodak_jpeg_load_raw : &kodak_dc120_load_raw;
        } else if ( !strcmp(GLOBAL_model, "DCS200") ) {
            thumb_height = 128;
            thumb_width = 192;
            thumb_offset = 6144;
            thumb_misc = 360;
            write_thumb = &layer_thumb;
                            ADOBE_black = 17;
        }
    } else if ( !strcmp(GLOBAL_model, "Fotoman Pixtura") ) {
        height = 512;
        width = 768;
                                                                                                                                                                                                                                                                                                                        GLOBAL_IO_profileOffset = 3632;
        TIFF_CALLBACK_loadRawData = &kodak_radc_load_raw;
                                                                                                                                                                                                                                                                                                                        IMAGE_filters = 0x61616161;
        simple_coeff(2);
    } else if ( !strncmp(GLOBAL_model, "QuickTake", 9) ) {
        if ( head[5] ) {
            strcpy(GLOBAL_model + 10, "200");
        }
        fseek(GLOBAL_IO_ifp, 544, SEEK_SET);
        height = read2bytes();
        width = read2bytes();
                                                                                                                                                                                                                                                                                                                            GLOBAL_IO_profileOffset = (read4bytes(), read2bytes()) == 30 ? 738 : 736;
        if ( height > width ) {
            SWAP(height, width);
            fseek(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset - 6, SEEK_SET);
            GLOBAL_flipsMask = ~read2bytes() & 3 ? 5 : 6;
        }
                                                                                                                                                                                                                                                                                                                            IMAGE_filters = 0x61616161;
    } else if ( !strcmp(GLOBAL_make, "Rollei") && !TIFF_CALLBACK_loadRawData ) {
        switch ( THE_image.width ) {
            case 1316:
                height = 1030;
                width = 1300;
                top_margin = 1;
                left_margin = 6;
                break;
            case 2568:
                height = 1960;
                width = 2560;
                top_margin = 2;
                left_margin = 8;
                break;
        }
                                                                                                                                                                                                                                                                                                                                IMAGE_filters = 0x16161616;
        TIFF_CALLBACK_loadRawData = &rollei_load_raw;
    }
    //-------------------------------------------------------------------------------------------------------------

    if ( !GLOBAL_model[0] ) {
        snprintf(GLOBAL_model, 64, "%dx%d", width, height);
    }

    if ( IMAGE_filters == UINT_MAX) {
        IMAGE_filters = 0x94949494;
    }

    if ( thumb_offset && !thumb_height ) {
        fseek(GLOBAL_IO_ifp, thumb_offset, SEEK_SET);
        if ( ljpeg_start(&jh, 1) ) {
            thumb_width = jh.wide;
            thumb_height = jh.high;
        }
    }

    dng_skip:
    if ( (OPTIONS_values->useCameraMatrix & (OPTIONS_values->useCameraWb || GLOBAL_dngVersion) ) && cmatrix[0][0] > 0.125 ) {
        memcpy(rgb_cam, cmatrix, sizeof cmatrix);
        GLOBAL_colorTransformForRaw = 0;
    }

    if ( GLOBAL_colorTransformForRaw ) {
        adobe_coeff(GLOBAL_make, GLOBAL_model);
    }

    if ( TIFF_CALLBACK_loadRawData == &kodak_radc_load_raw ) {
        if ( GLOBAL_colorTransformForRaw ) {
            adobe_coeff("Apple", "Quicktake");
        }
    }

    if ( fuji_width ) {
        fuji_width = width >> !fuji_layout;
        IMAGE_filters = fuji_width & 1 ? 0x94949494 : 0x49494949;
        width = (height >> fuji_layout) + fuji_width;
        height = width - 1;
        pixel_aspect = 1;
    } else {
        if ( THE_image.height < height ) {
            THE_image.height = height;
        }
        if ( THE_image.width < width ) {
            THE_image.width = width;
        }
    }

    if ( !THE_image.bitsPerSample ) {
        THE_image.bitsPerSample = 12;
    }

    if ( !ADOBE_maximum ) {
        ADOBE_maximum = (1 << THE_image.bitsPerSample) - 1;
    }

    if ( !TIFF_CALLBACK_loadRawData || height < 22 || width < 22 ||
         THE_image.bitsPerSample > 16 || tiff_samples > 6 || IMAGE_colors > 4 ) {
        is_raw = 0;
    }

#ifdef NO_JASPER
    if ( TIFF_CALLBACK_loadRawData == &redcine_load_raw ) {
        fprintf(stderr, _("%s: You must link dcraw with %s!!\n"),
                CAMERA_IMAGE_information.inputFilename, "libjasper");
        is_raw = 0;
    }
#endif

#ifdef NO_JPEG
    if (TIFF_CALLBACK_loadRawData == &kodak_jpeg_load_raw || TIFF_CALLBACK_loadRawData == &lossy_dng_load_raw) {
        fprintf (stderr,_("%s: You must link dcraw with %s!!\n"),
        CAMERA_IMAGE_information.inputFilename, "libjpeg");
        is_raw = 0;
    }
#endif

    if ( !GLOBAL_bayerPatternLabels[0] ) {
        strcpy(GLOBAL_bayerPatternLabels, IMAGE_colors == 3 ? "RGBG" : "GMCY");
    }

    if ( !THE_image.height ) {
        THE_image.height = height;
    }

    if ( !THE_image.width ) {
        THE_image.width = width;
    }

    if ( IMAGE_filters > 999 && IMAGE_colors == 3 ) {
        IMAGE_filters |= ((IMAGE_filters >> 2 & 0x22222222) |
                          (IMAGE_filters << 2 & 0x88888888)) & IMAGE_filters << 1;
    }

    notraw:
    if ( GLOBAL_flipsMask == UINT_MAX ) {
        GLOBAL_flipsMask = cameraFlip;
    }

    if ( GLOBAL_flipsMask == UINT_MAX ) {
        GLOBAL_flipsMask = 0;
    }
}

#ifndef NO_LCMS

void
apply_profile(const char *input, const char *output) {
    char *prof;
    cmsHPROFILE hInProfile = 0;
    cmsHPROFILE hOutProfile = 0;
    cmsHTRANSFORM hTransform;
    FILE *fp;
    unsigned size;

    if ( strcmp(input, "embed") ) {
        hInProfile = cmsOpenProfileFromFile(input, "r");
    } else {
        if ( profile_length ) {
            prof = (char *) malloc(profile_length);
            memoryError(prof, "apply_profile()");
            fseek(GLOBAL_IO_ifp, profile_offset, SEEK_SET);
            fread(prof, 1, profile_length, GLOBAL_IO_ifp);
            hInProfile = cmsOpenProfileFromMem(prof, profile_length);
            free(prof);
        } else {
            fprintf(stderr, _("%s has no embedded profile.\n"), CAMERA_IMAGE_information.inputFilename);
        }
    }

    if ( !hInProfile ) {
        return;
    }

    if ( !output ) {
        hOutProfile = cmsCreate_sRGBProfile();
    } else {
        if ( (fp = fopen(output, "rb")) ) {
            fread(&size, 4, 1, fp);
            fseek(fp, 0, SEEK_SET);
            GLOBAL_outputIccProfile = (unsigned *) malloc(size = ntohl(size));
            memoryError(GLOBAL_outputIccProfile, "apply_profile()");
            fread(GLOBAL_outputIccProfile, 1, size, fp);
            fclose(fp);
            if ( !(hOutProfile = cmsOpenProfileFromMem(GLOBAL_outputIccProfile, size))) {
                free(GLOBAL_outputIccProfile);
                GLOBAL_outputIccProfile = 0;
            }
        } else {
            fprintf(stderr, _("Cannot open file %s!\n"), output);
        }
    }

    if ( !hOutProfile ) {
        goto quit;
    }
    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, _("Applying color profile...\n"));
    }
    hTransform = cmsCreateTransform(hInProfile, TYPE_RGBA_16,
                                    hOutProfile, TYPE_RGBA_16, INTENT_PERCEPTUAL, 0);
    cmsDoTransform(hTransform, GLOBAL_image, GLOBAL_image, width * height);
    GLOBAL_colorTransformForRaw = 1;        /* Don't use rgb_cam with a profile */
    cmsDeleteTransform(hTransform);
    cmsCloseProfile(hOutProfile);
    quit:
    cmsCloseProfile(hInProfile);
}

#endif

void
convert_to_rgb() {
    int row;
    int col;
    int c;
    int i;
    int j;
    int k;
    unsigned short *img;
    float out[3];
    float out_cam[3][4];
    double num;
    double inverse[3][3];
    static const double xyzd50_srgb[3][3] =
            {{0.436083, 0.385083, 0.143055},
             {0.222507, 0.716888, 0.060608},
             {0.013930, 0.097097, 0.714022}};
    static const double rgb_rgb[3][3] =
            {{1, 0, 0},
             {0, 1, 0},
             {0, 0, 1}};
    static const double adobe_rgb[3][3] =
            {{0.715146, 0.284856, 0.000000},
             {0.000000, 1.000000, 0.000000},
             {0.000000, 0.041166, 0.958839}};
    static const double wide_rgb[3][3] =
            {{0.593087, 0.404710, 0.002206},
             {0.095413, 0.843149, 0.061439},
             {0.011621, 0.069091, 0.919288}};
    static const double prophoto_rgb[3][3] =
            {{0.529317, 0.330092, 0.140588},
             {0.098368, 0.873465, 0.028169},
             {0.016879, 0.117663, 0.865457}};
    static const double aces_rgb[3][3] =
            {{0.432996, 0.375380, 0.189317},
             {0.089427, 0.816523, 0.102989},
             {0.019165, 0.118150, 0.941914}};
    static const double (*out_rgb[])[3] =
            {rgb_rgb, adobe_rgb, wide_rgb, prophoto_rgb, xyz_rgb, aces_rgb};
    static const char *name[] =
            {"sRGB", "Adobe RGB (1998)", "WideGamut D65", "ProPhoto D65", "XYZ", "ACES"};
    static const unsigned phead[] =
            {1024, 0, 0x2100000, 0x6d6e7472, 0x52474220, 0x58595a20, 0, 0, 0,
             0x61637370, 0, 0, 0x6e6f6e65, 0, 0, 0, 0, 0xf6d6, 0x10000, 0xd32d};
    unsigned pbody[] =
            {10, 0x63707274, 0, 36,    // cprt
             0x64657363, 0, 40,    // desc
             0x77747074, 0, 20,    // wtpt
             0x626b7074, 0, 20,    // bkpt
             0x72545243, 0, 14,    // rTRC
             0x67545243, 0, 14,    // gTRC
             0x62545243, 0, 14,    // bTRC
             0x7258595a, 0, 20,    // rXYZ
             0x6758595a, 0, 20,    // gXYZ
             0x6258595a, 0, 20};    // bXYZ
    static const unsigned pwhite[] = {0xf351, 0x10000, 0x116cc};
    unsigned pcurve[] = {0x63757276, 0, 1, 0x1000000};

    gamma_curve(OPTIONS_values->gammaParameters[0], OPTIONS_values->gammaParameters[1], 0, 0);
    memcpy(out_cam, rgb_cam, sizeof out_cam);
    GLOBAL_colorTransformForRaw |= IMAGE_colors == 1 || OPTIONS_values->documentMode ||
                                   OPTIONS_values->outputColorSpace < 1 || OPTIONS_values->outputColorSpace > 6;

    if ( !GLOBAL_colorTransformForRaw ) {
        GLOBAL_outputIccProfile = (unsigned *) calloc(phead[0], 1);
        memoryError(GLOBAL_outputIccProfile, "convert_to_rgb()");
        memcpy(GLOBAL_outputIccProfile, phead, sizeof phead);
        if ( OPTIONS_values->outputColorSpace == 5 ) GLOBAL_outputIccProfile[4] = GLOBAL_outputIccProfile[5];
        GLOBAL_outputIccProfile[0] = 132 + 12 * pbody[0];
        for ( i = 0; i < pbody[0]; i++ ) {
            GLOBAL_outputIccProfile[GLOBAL_outputIccProfile[0] / 4] = i ? (i > 1 ? 0x58595a20 : 0x64657363)
                                                                        : 0x74657874;
            pbody[i * 3 + 2] = GLOBAL_outputIccProfile[0];
            GLOBAL_outputIccProfile[0] += (pbody[i * 3 + 3] + 3) & -4;
        }
        memcpy(GLOBAL_outputIccProfile + 32, pbody, sizeof pbody);
        GLOBAL_outputIccProfile[pbody[5] / 4 + 2] = strlen(name[OPTIONS_values->outputColorSpace - 1]) + 1;
        memcpy((char *) GLOBAL_outputIccProfile + pbody[8] + 8, pwhite, sizeof pwhite);
        pcurve[3] = (short) (256 / OPTIONS_values->gammaParameters[5] + 0.5) << 16;
        for ( i = 4; i < 7; i++ ) {
            memcpy((char *) GLOBAL_outputIccProfile + pbody[i * 3 + 2], pcurve, sizeof pcurve);
        }
        pseudoinverse((double (*)[3]) out_rgb[OPTIONS_values->outputColorSpace - 1], inverse, 3);
        for ( i = 0; i < 3; i++ ) {
            for ( j = 0; j < 3; j++ ) {
                for ( num = k = 0; k < 3; k++ ) {
                    num += xyzd50_srgb[i][k] * inverse[j][k];
                }
                GLOBAL_outputIccProfile[pbody[j * 3 + 23] / 4 + i + 2] = num * 0x10000 + 0.5;
            }
        }
        for ( i = 0; i < phead[0] / 4; i++ ) {
            GLOBAL_outputIccProfile[i] = htonl(GLOBAL_outputIccProfile[i]);
        }
        strcpy((char *) GLOBAL_outputIccProfile + pbody[2] + 8, "auto-generated by dcraw");
        strcpy((char *) GLOBAL_outputIccProfile + pbody[5] + 12, name[OPTIONS_values->outputColorSpace - 1]);
        for ( i = 0; i < 3; i++ ) {
            for ( j = 0; j < IMAGE_colors; j++ ) {
                for ( out_cam[i][j] = k = 0; k < 3; k++ ) {
                    out_cam[i][j] += out_rgb[OPTIONS_values->outputColorSpace - 1][i][k] * rgb_cam[k][j];
                }
            }
        }
    }

    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, GLOBAL_colorTransformForRaw ? _("Building histograms...\n") :
                        _("Converting to %s colorspace...\n"), name[OPTIONS_values->outputColorSpace - 1]);
    }

    memset(histogram, 0, sizeof histogram);
    for ( img = GLOBAL_image[0], row = 0; row < height; row++ ) {
        for ( col = 0; col < width; col++, img += 4 ) {
            if ( !GLOBAL_colorTransformForRaw ) {
                out[0] = out[1] = out[2] = 0;
                for ( c = 0; c < IMAGE_colors; c++ ) {
                    out[0] += out_cam[0][c] * img[c];
                    out[1] += out_cam[1][c] * img[c];
                    out[2] += out_cam[2][c] * img[c];
                }
                for ( c = 0; c < 3; c++ ) {
                    img[c] = CLIP((int) out[c]);
                }
            } else {
                if ( OPTIONS_values->documentMode ) {
                    img[0] = img[fcol(row, col)];
                }
            }
            for ( c = 0; c < IMAGE_colors; c++ ) {
                histogram[c][img[c] >> 3]++;
            }
        }
    }
    if ( IMAGE_colors == 4 && OPTIONS_values->outputColorSpace ) {
        IMAGE_colors = 3;
    }
    if ( OPTIONS_values->documentMode && IMAGE_filters ) {
        IMAGE_colors = 1;
    }
}

void
fuji_rotate() {
    int i;
    int row;
    int col;
    double step;
    float r;
    float c;
    float fr;
    float fc;
    unsigned ur;
    unsigned uc;
    unsigned short wide;
    unsigned short high;
    unsigned short (*img)[4];
    unsigned short (*pix)[4];

    if ( !fuji_width ) {
        return;
    }
    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, _("Rotating GLOBAL_image 45 degrees...\n"));
    }
    fuji_width = (fuji_width - 1 + IMAGE_shrink) >> IMAGE_shrink;
    step = sqrt(0.5);
    wide = fuji_width / step;
    high = (height - fuji_width) / step;
    img = (unsigned short (*)[4]) calloc(high, wide * sizeof *img);
    memoryError(img, "fuji_rotate()");

    for ( row = 0; row < high; row++ ) {
        for ( col = 0; col < wide; col++ ) {
            ur = r = fuji_width + (row - col) * step;
            uc = c = (row + col) * step;
            if ( ur > height - 2 || uc > width - 2 ) {
                continue;
            }
            fr = r - ur;
            fc = c - uc;
            pix = GLOBAL_image + ur * width + uc;
            for ( i = 0; i < IMAGE_colors; i++ ) {
                img[row * wide + col][i] =
                        (pix[0][i] * (1 - fc) + pix[1][i] * fc) * (1 - fr) +
                        (pix[width][i] * (1 - fc) + pix[width + 1][i] * fc) * fr;
            }
        }
    }
    free(GLOBAL_image);
    width = wide;
    height = high;
    GLOBAL_image = img;
    fuji_width = 0;
}

void
stretch() {
    unsigned short newdim;
    unsigned short (*img)[4];
    unsigned short *pix0;
    unsigned short *pix1;
    int row;
    int col;
    int c;
    double rc;
    double frac;

    if ( pixel_aspect == 1 ) {
        return;
    }
    if ( OPTIONS_values->verbose ) {
        fprintf(stderr, _("Stretching the GLOBAL_image...\n"));
    }
    if ( pixel_aspect < 1 ) {
        newdim = height / pixel_aspect + 0.5;
        img = (unsigned short (*)[4]) calloc(width, newdim * sizeof *img);
        memoryError(img, "stretch()");
        for ( rc = row = 0; row < newdim; row++, rc += pixel_aspect ) {
            frac = rc - (c = rc);
            pix0 = pix1 = GLOBAL_image[c * width];
            if ( c + 1 < height ) {
                pix1 += width * 4;
            }
            for ( col = 0; col < width; col++, pix0 += 4, pix1 += 4 ) {
                for ( c = 0; c < IMAGE_colors; c++ ) {
                    img[row * width + col][c] = pix0[c] * (1 - frac) + pix1[c] * frac + 0.5;
                }
            }
        }
        height = newdim;
    } else {
        newdim = width * pixel_aspect + 0.5;
        img = (unsigned short (*)[4]) calloc(height, newdim * sizeof *img);
        memoryError(img, "stretch()");
        for ( rc = col = 0; col < newdim; col++, rc += 1 / pixel_aspect ) {
            frac = rc - (c = rc);
            pix0 = pix1 = GLOBAL_image[c];
            if ( c + 1 < width ) {
                pix1 += 4;
            }
            for ( row = 0; row < height; row++, pix0 += width * 4, pix1 += width * 4 ) {
                for ( c = 0; c < IMAGE_colors; c++ ) {
                    img[row * newdim + col][c] = pix0[c] * (1 - frac) + pix1[c] * frac + 0.5;
                }
            }
        }
        width = newdim;
    }
    free(GLOBAL_image);
    GLOBAL_image = img;
}

int
flip_index(int row, int col) {
    if ( GLOBAL_flipsMask & 4 ) {
        SWAP(row, col);
    }
    if ( GLOBAL_flipsMask & 2 ) {
        row = IMAGE_iheight - 1 - row;
    }
    if ( GLOBAL_flipsMask & 1 ) {
        col = IMAGE_iwidth - 1 - col;
    }
    return row * IMAGE_iwidth + col;
}

struct tiff_tag {
    unsigned short tag;
    unsigned short type;
    int count;
    union {
        char c[4];
        short s[2];
        int i;
    } val;
};

struct tiff_hdr {
    unsigned short order;
    unsigned short magic;
    int ifd;
    unsigned short pad;
    unsigned short ntag;
    struct tiff_tag tag[23];
    int nextifd;
    unsigned short pad2;
    unsigned short nexif;
    struct tiff_tag exif[4];
    unsigned short pad3;
    unsigned short ngps;
    struct tiff_tag gpst[10];
    short bps[4];
    int rat[10];
    unsigned gps[26];
    char desc[512];
    char make[64];
    char model[64];
    char soft[32];
    char date[20];
    char artist[64];
};

void
tiff_set(struct tiff_hdr *th, unsigned short *ntag, unsigned short tag, unsigned short type, int count, int val) {
    struct tiff_tag *tt;
    int c;

    tt = (struct tiff_tag *) (ntag + 1) + (*ntag)++;
    tt->val.i = val;
    if ( type == 1 && count <= 4 ) {
        for ( c = 0; c < 4; c++ ) {
            tt->val.c[c] = val >> (c << 3);
        }
    } else {
        if ( type == 2 ) {
            count = strnlen((char *) th + val, count - 1) + 1;
            if ( count <= 4 ) {
                for ( c = 0; c < 4; c++ ) {
                    tt->val.c[c] = ((char *) th)[val + c];
                }
            }
        } else {
            if ( type == 3 && count <= 2 ) {
                for ( c = 0; c < 2; c++ ) {
                    tt->val.s[c] = val >> (c << 4);
                }
            }
        }
    }
    tt->count = count;
    tt->type = type;
    tt->tag = tag;
}

#define TOFF(ptr) ((char *)(&(ptr)) - (char *)th)

void
tiff_head(struct tiff_hdr *th, int full) {
    int c;
    int psize = 0;
    struct tm *t;

    memset(th, 0, sizeof *th);
    th->order = htonl(0x4d4d4949) >> 16;
    th->magic = 42;
    th->ifd = 10;
    th->rat[0] = th->rat[2] = 300;
    th->rat[1] = th->rat[3] = 1;
    for ( c = 0; c < 6; c++ ) {
        th->rat[4 + c] = 1000000;
    }
    th->rat[4] *= CAMERA_IMAGE_information.shutterSpeed;
    th->rat[6] *= aperture;
    th->rat[8] *= focal_len;
    strncpy(th->desc, desc, 512);
    strncpy(th->make, GLOBAL_make, 64);
    strncpy(th->model, GLOBAL_model, 64);
    strcpy(th->soft, "dcraw v"DCRAW_VERSION);
    t = localtime(&timestamp);
    snprintf(th->date, 20, "%04d:%02d:%02d %02d:%02d:%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    strncpy(th->artist, artist, 64);
    if ( full ) {
        tiff_set(th, &th->ntag, 254, 4, 1, 0);
        tiff_set(th, &th->ntag, 256, 4, 1, width);
        tiff_set(th, &th->ntag, 257, 4, 1, height);
        tiff_set(th, &th->ntag, 258, 3, IMAGE_colors, OPTIONS_values->outputBitsPerPixel);
        if ( IMAGE_colors > 2 ) {
            th->tag[th->ntag - 1].val.i = TOFF(th->bps);
        }
        for ( c = 0; c < 4; c++ ) {
            th->bps[c] = OPTIONS_values->outputBitsPerPixel;
        }
        tiff_set(th, &th->ntag, 259, 3, 1, 1);
        tiff_set(th, &th->ntag, 262, 3, 1, 1 + (IMAGE_colors > 1));
    }
    tiff_set(th, &th->ntag, 270, 2, 512, TOFF(th->desc));
    tiff_set(th, &th->ntag, 271, 2, 64, TOFF(th->make));
    tiff_set(th, &th->ntag, 272, 2, 64, TOFF(th->model));
    if ( full ) {
        if ( GLOBAL_outputIccProfile ) {
            psize = ntohl(GLOBAL_outputIccProfile[0]);
        }
        tiff_set(th, &th->ntag, 273, 4, 1, sizeof *th + psize);
        tiff_set(th, &th->ntag, 277, 3, 1, IMAGE_colors);
        tiff_set(th, &th->ntag, 278, 4, 1, height);
        tiff_set(th, &th->ntag, 279, 4, 1, height * width * IMAGE_colors * OPTIONS_values->outputBitsPerPixel / 8);
    } else {
        tiff_set(th, &th->ntag, 274, 3, 1, "12435867"[GLOBAL_flipsMask] - '0');
    }
    tiff_set(th, &th->ntag, 282, 5, 1, TOFF(th->rat[0]));
    tiff_set(th, &th->ntag, 283, 5, 1, TOFF(th->rat[2]));
    tiff_set(th, &th->ntag, 284, 3, 1, 1);
    tiff_set(th, &th->ntag, 296, 3, 1, 2);
    tiff_set(th, &th->ntag, 305, 2, 32, TOFF(th->soft));
    tiff_set(th, &th->ntag, 306, 2, 20, TOFF(th->date));
    tiff_set(th, &th->ntag, 315, 2, 64, TOFF(th->artist));
    tiff_set(th, &th->ntag, 34665, 4, 1, TOFF(th->nexif));
    if ( psize ) {
        tiff_set(th, &th->ntag, 34675, 7, psize, sizeof *th);
    }
    tiff_set(th, &th->nexif, 33434, 5, 1, TOFF(th->rat[4]));
    tiff_set(th, &th->nexif, 33437, 5, 1, TOFF(th->rat[6]));
    tiff_set(th, &th->nexif, 34855, 3, 1, iso_speed);
    tiff_set(th, &th->nexif, 37386, 5, 1, TOFF(th->rat[8]));
    if ( gpsdata[1] ) {
        tiff_set(th, &th->ntag, 34853, 4, 1, TOFF(th->ngps));
        tiff_set(th, &th->ngps, 0, 1, 4, 0x202);
        tiff_set(th, &th->ngps, 1, 2, 2, gpsdata[29]);
        tiff_set(th, &th->ngps, 2, 5, 3, TOFF(th->gps[0]));
        tiff_set(th, &th->ngps, 3, 2, 2, gpsdata[30]);
        tiff_set(th, &th->ngps, 4, 5, 3, TOFF(th->gps[6]));
        tiff_set(th, &th->ngps, 5, 1, 1, gpsdata[31]);
        tiff_set(th, &th->ngps, 6, 5, 1, TOFF(th->gps[18]));
        tiff_set(th, &th->ngps, 7, 5, 3, TOFF(th->gps[12]));
        tiff_set(th, &th->ngps, 18, 2, 12, TOFF(th->gps[20]));
        tiff_set(th, &th->ngps, 29, 2, 12, TOFF(th->gps[23]));
        memcpy(th->gps, gpsdata, sizeof th->gps);
    }
}

void
jpeg_thumb() {
    char *thumb;
    unsigned short exif[5];
    struct tiff_hdr th;

    thumb = (char *) malloc(thumb_length);
    memoryError(thumb, "jpeg_thumb()");
    fread(thumb, 1, thumb_length, GLOBAL_IO_ifp);
    fputc(0xff, ofp);
    fputc(0xd8, ofp);
    if ( strcmp(thumb + 6, "Exif") ) {
        memcpy(exif, "\xff\xe1  Exif\0\0", 10);
        exif[1] = htons (8 + sizeof th);
        fwrite(exif, 1, sizeof exif, ofp);
        tiff_head(&th, 0);
        fwrite(&th, 1, sizeof th, ofp);
    }
    fwrite(thumb + 2, 1, thumb_length - 2, ofp);
    free(thumb);
}

void
write_ppm_tiff() {
    struct tiff_hdr th;
    unsigned char *ppm;
    unsigned short *ppm2;
    int c;
    int row;
    int col;
    int soff;
    int rstep;
    int cstep;
    int perc;
    int val;
    int total;
    int ppmWhite = 0x2000;

    perc = width * height * 0.01; // 99th percentile ppmWhite level
    if ( fuji_width ) {
        perc /= 2;
    }
    if ( !((OPTIONS_values->highlight & ~2) || OPTIONS_values->noAutoBright) ) {
        for ( ppmWhite = c = 0; c < IMAGE_colors; c++ ) {
            for ( val = 0x2000, total = 0; --val > 32; ) {
                if ((total += histogram[c][val]) > perc ) {
                    break;
                }
            }
            if ( ppmWhite < val ) {
                ppmWhite = val;
            }
        }
    }
    gamma_curve(OPTIONS_values->gammaParameters[0], OPTIONS_values->gammaParameters[1], 2,
                (ppmWhite << 3) / OPTIONS_values->brightness);
    IMAGE_iheight = height;
    IMAGE_iwidth = width;
    if ( GLOBAL_flipsMask & 4 ) {
        SWAP(height, width);
    }
    ppm = (unsigned char *)calloc(width, IMAGE_colors * OPTIONS_values->outputBitsPerPixel / 8);
    ppm2 = (unsigned short *) ppm;
    memoryError(ppm, "write_ppm_tiff()");
    if ( OPTIONS_values->outputTiff ) {
        tiff_head(&th, 1);
        fwrite(&th, sizeof th, 1, ofp);
        if ( GLOBAL_outputIccProfile ) {
            fwrite(GLOBAL_outputIccProfile, ntohl(GLOBAL_outputIccProfile[0]), 1, ofp);
        }
    } else {
        if ( IMAGE_colors > 3 ) {
            fprintf(ofp,
                    "P7\nWIDTH %d\nHEIGHT %d\nDEPTH %d\nMAXVAL %d\nTUPLTYPE %s\nENDHDR\n",
                    width, height, IMAGE_colors, (1 << OPTIONS_values->outputBitsPerPixel) - 1, GLOBAL_bayerPatternLabels);
        } else {
            fprintf(ofp, "P%d\n%d %d\n%d\n",
                    IMAGE_colors / 2 + 5, width, height, (1 << OPTIONS_values->outputBitsPerPixel) - 1);
        }
    }
    soff = flip_index(0, 0);
    cstep = flip_index(0, 1) - soff;
    rstep = flip_index(1, 0) - flip_index(0, width);
    for ( row = 0; row < height; row++, soff += rstep ) {
        for ( col = 0; col < width; col++, soff += cstep ) {
            if ( OPTIONS_values->outputBitsPerPixel == 8 ) {
                for ( c = 0; c < IMAGE_colors; c++ ) {
                    ppm[col * IMAGE_colors + c] = GAMMA_curveFunctionLookupTable[GLOBAL_image[soff][c]] >> 8;
                }
            } else {
                for ( c = 0; c < IMAGE_colors; c++ ) {
                    ppm2[col * IMAGE_colors + c] = GAMMA_curveFunctionLookupTable[GLOBAL_image[soff][c]];
                }
            }
        }
        if ( OPTIONS_values->outputBitsPerPixel == 16 && !OPTIONS_values->outputTiff && htons(0x55aa) != 0x55aa ) {
            swab(ppm2, ppm2, width * IMAGE_colors * 2);
        }
        fwrite(ppm, IMAGE_colors * OPTIONS_values->outputBitsPerPixel / 8, width, ofp);
    }
    free(ppm);
}

int
main(int argc, const char **argv) {
    OPTIONS_values = new Options();
    int status = 0;
    int quality;
    int i;
    int c;
    const char *sp;
    char opm;
    char opt;
    char *ofname;
    char *cp;
    struct utimbuf ut;

#ifndef LOCALTIME
    putenv((char *) "TZ=UTC");
#endif
#ifdef LOCALEDIR
  setlocale (LC_CTYPE, "");
  setlocale (LC_MESSAGES, "");
  bindtextdomain ("dcraw", LOCALEDIR);
  textdomain ("dcraw");
#endif

    int arg = OPTIONS_values->setArguments(argc, argv);

    if ( OPTIONS_values->write_to_stdout ) {
        if ( isatty(1) ) {
            fprintf(stderr, _("Will not write an GLOBAL_image to the terminal!\n"));
            return 1;
        }
#if defined(WIN32) || defined(DJGPP) || defined(__CYGWIN__)
                                                                                                                                if (setmode(1,O_BINARY) < 0) {
      perror ("setmode()");
      return 1;
    }
#endif
    }
    for ( ; arg < argc; arg++ ) {
        status = 1;
        THE_image.rawData = 0;
        GLOBAL_image = 0;
        GLOBAL_outputIccProfile = 0;
        meta_data = ofname = 0;
        ofp = stdout;
        if ( setjmp (failure) ) {
            if ( fileno(GLOBAL_IO_ifp) > 2 ) {
                fclose(GLOBAL_IO_ifp);
            }
            if ( fileno(ofp) > 2 ) {
                fclose(ofp);
            }
            status = 1;
            goto cleanup;
        }

        // Open next raw IMAGE_array file from filename on arguments array
        if ( CAMERA_IMAGE_information.inputFilename ) {
            delete CAMERA_IMAGE_information.inputFilename;
            CAMERA_IMAGE_information.inputFilename = nullptr;
        }
        CAMERA_IMAGE_information.inputFilename = new char[strlen(argv[arg]) + 1];
        strcpy(CAMERA_IMAGE_information.inputFilename, argv[arg]);
        if ( !(GLOBAL_IO_ifp = fopen(CAMERA_IMAGE_information.inputFilename, "rb")) ) {
            perror(CAMERA_IMAGE_information.inputFilename);
            continue;
        }
        status = (tiffIdentify(), !is_raw);
        if ( OPTIONS_values->user_flip >= 0 ) {
            GLOBAL_flipsMask = OPTIONS_values->user_flip;
        }
        switch ((GLOBAL_flipsMask + 3600) % 360 ) {
            case 270:
                GLOBAL_flipsMask = 5;
                break;
            case 180:
                GLOBAL_flipsMask = 3;
                break;
            case 90:
                GLOBAL_flipsMask = 6;
        }
        if ( OPTIONS_values->timestamp_only ) {
            if ( (status = !timestamp) ) {
                fprintf(stderr, _("%s has no timestamp.\n"), CAMERA_IMAGE_information.inputFilename);
            } else {
                if ( OPTIONS_values->identify_only ) {
                    printf("%10ld%10d %s\n", (long) timestamp, shot_order, CAMERA_IMAGE_information.inputFilename);
                } else {
                    if ( OPTIONS_values->verbose )
                        fprintf(stderr, _("%s time set to %d.\n"), CAMERA_IMAGE_information.inputFilename, (int) timestamp);
                    ut.actime = ut.modtime = timestamp;
                    utime(CAMERA_IMAGE_information.inputFilename, &ut);
                }
            }
            goto next;
        }
        write_fun = &write_ppm_tiff;
        if ( OPTIONS_values->thumbnail_only ) {
            if ( (status = !thumb_offset) ) {
                fprintf(stderr, _("%s has no thumbnail.\n"), CAMERA_IMAGE_information.inputFilename);
                goto next;
            } else {
                if ( CALLBACK_loadThumbnailRawData ) {
                    // Will work over RAW GLOBAL_image on following code
                    TIFF_CALLBACK_loadRawData = CALLBACK_loadThumbnailRawData;
                    GLOBAL_IO_profileOffset = thumb_offset;
                    height = thumb_height;
                    width = thumb_width;
                    IMAGE_filters = 0;
                    IMAGE_colors = 3;
                } else {
                    fseek(GLOBAL_IO_ifp, thumb_offset, SEEK_SET);
                    write_fun = write_thumb;
                    goto thumbnail;
                }
            }
        }
        if ( TIFF_CALLBACK_loadRawData == &kodak_ycbcr_load_raw ) {
            height += height & 1;
            width += width & 1;
        }
        if ( OPTIONS_values->identify_only && OPTIONS_values->verbose && GLOBAL_make[0] ) {
            printf(_("\nFilename: %s\n"), CAMERA_IMAGE_information.inputFilename);
            printf(_("Timestamp: %s"), ctime(&timestamp));
            printf(_("Camera: %s %s\n"), GLOBAL_make, GLOBAL_model);
            if ( artist[0] ) {
                printf(_("Owner: %s\n"), artist);
            }
            if ( GLOBAL_dngVersion ) {
                printf(_("DNG Version: "));
                for ( i = 24; i >= 0; i -= 8 ) {
                    printf("%d%c", GLOBAL_dngVersion >> i & 255, i ? '.' : '\n');
                }
            }
            printf(_("ISO speed: %d\n"), (int) iso_speed);
            printf(_("Shutter: "));
            if ( CAMERA_IMAGE_information.shutterSpeed > 0 && CAMERA_IMAGE_information.shutterSpeed < 1 ) {
                CAMERA_IMAGE_information.shutterSpeed = (printf("1/"), 1 / CAMERA_IMAGE_information.shutterSpeed);
            }
            printf(_("%0.1f sec\n"), CAMERA_IMAGE_information.shutterSpeed);
            printf(_("Aperture: f/%0.1f\n"), aperture);
            printf(_("Focal length: %0.1f mm\n"), focal_len);
            printf(_("Embedded ICC profile: %s\n"), profile_length ? _("yes") : _("no"));
            printf(_("Number of raw images: %d\n"), is_raw);
            if ( pixel_aspect != 1 ) {
                printf(_("Pixel Aspect Ratio: %0.6f\n"), pixel_aspect);
            }
            if ( thumb_offset ) {
                printf(_("Thumb size:  %4d x %d\n"), thumb_width, thumb_height);
            }
            printf(_("Full size:   %4d x %d\n"), THE_image.width, THE_image.height);
        } else {
            if ( !is_raw ) {
                fprintf(stderr, _("Cannot decode file %s\n"), CAMERA_IMAGE_information.inputFilename);
            }
        }
        if ( !is_raw ) {
            goto next;
        }
        IMAGE_shrink = IMAGE_filters && (OPTIONS_values->halfSizePreInterpolation || (!OPTIONS_values->identify_only &&
                                                                                      (OPTIONS_values->threshold || OPTIONS_values->chromaticAberrationCorrection[0] != 1 ||
                           OPTIONS_values->chromaticAberrationCorrection[2] != 1)));
        IMAGE_iheight = (height + IMAGE_shrink) >> IMAGE_shrink;
        IMAGE_iwidth = (width + IMAGE_shrink) >> IMAGE_shrink;
        if ( OPTIONS_values->identify_only ) {
            if ( OPTIONS_values->verbose ) {
                if ( OPTIONS_values->documentMode == 3 ) {
                    top_margin = left_margin = fuji_width = 0;
                    height = THE_image.height;
                    width = THE_image.width;
                }
                IMAGE_iheight = (height + IMAGE_shrink) >> IMAGE_shrink;
                IMAGE_iwidth = (width + IMAGE_shrink) >> IMAGE_shrink;
                if ( OPTIONS_values->useFujiRotate ) {
                    if ( fuji_width ) {
                        fuji_width = (fuji_width - 1 + IMAGE_shrink) >> IMAGE_shrink;
                        IMAGE_iwidth = fuji_width / sqrt(0.5);
                        IMAGE_iheight = (IMAGE_iheight - fuji_width) / sqrt(0.5);
                    } else {
                        if ( pixel_aspect < 1 ) IMAGE_iheight = IMAGE_iheight / pixel_aspect + 0.5;
                        if ( pixel_aspect > 1 ) IMAGE_iwidth = IMAGE_iwidth * pixel_aspect + 0.5;
                    }
                }
                if ( GLOBAL_flipsMask & 4 ) {
                    SWAP(IMAGE_iheight, IMAGE_iwidth);
                }
                printf(_("Image size:  %4d x %d\n"), width, height);
                printf(_("Output size: %4d x %d\n"), IMAGE_iwidth, IMAGE_iheight);
                printf(_("Raw colors: %d"), IMAGE_colors);
                if ( IMAGE_filters ) {
                    int fhigh = 2, fwide = 2;
                    if ((IMAGE_filters ^ (IMAGE_filters >> 8)) & 0xff ) fhigh = 4;
                    if ((IMAGE_filters ^ (IMAGE_filters >> 16)) & 0xffff ) fhigh = 8;
                    if ( IMAGE_filters == 1 ) fhigh = fwide = 16;
                    if ( IMAGE_filters == 9 ) fhigh = fwide = 6;
                    printf(_("\nFilter pattern: "));
                    for ( i = 0; i < fhigh; i++ ) {
                        for ( c = i && putchar('/') && 0; c < fwide; c++ ) {
                            putchar(GLOBAL_bayerPatternLabels[fcol(i, c)]);
                        }
                    }
                }
                printf(_("\nDaylight multipliers:"));
                for ( c = 0; c < IMAGE_colors; c++ ) {
                    printf(" %f", pre_mul[c]);
                }
                if ( GLOBAL_cam_mul[0] > 0 ) {
                    printf(_("\nCamera multipliers:"));
                    for ( c = 0; c < 4; c++ ) {
                        printf(" %f", GLOBAL_cam_mul[c]);
                    }
                }
                putchar('\n');
            } else {
                printf(_("%s is a %s %s GLOBAL_image.\n"), CAMERA_IMAGE_information.inputFilename, GLOBAL_make, GLOBAL_model);
            }
            next:
            fclose(GLOBAL_IO_ifp);
            continue;
        }
        if ( meta_length ) {
            meta_data = (char *) malloc(meta_length);
            memoryError(meta_data, "main()");
        }
        if ( IMAGE_filters || IMAGE_colors == 1 ) {
            THE_image.rawData = (unsigned short *) calloc((THE_image.height + 7), THE_image.width * 2);
            memoryError(THE_image.rawData, "main()");
        } else {
            GLOBAL_image = (unsigned short (*)[4]) calloc(IMAGE_iheight, IMAGE_iwidth * sizeof *GLOBAL_image);
            memoryError(GLOBAL_image, "main()");
        }
        if ( OPTIONS_values->verbose ) {
            fprintf(stderr, _("Loading %s %s image from %s ...\n"), GLOBAL_make, GLOBAL_model, CAMERA_IMAGE_information.inputFilename);
        }
        if ( OPTIONS_values->shotSelect >= is_raw ) {
            fprintf(stderr, _("%s: \"-s %d\" requests a nonexistent GLOBAL_image!\n"), CAMERA_IMAGE_information.inputFilename, OPTIONS_values->shotSelect);
        }
        fseeko(GLOBAL_IO_ifp, GLOBAL_IO_profileOffset, SEEK_SET);
        if ( THE_image.rawData && OPTIONS_values->readFromStdin ) {
            fread(THE_image.rawData, 2, THE_image.height * THE_image.width, stdin);
        }
        else {
            (*TIFF_CALLBACK_loadRawData)();
        }
        if ( OPTIONS_values->documentMode == 3 ) {
            top_margin = left_margin = fuji_width = 0;
            height = THE_image.height;
            width = THE_image.width;
        }
        IMAGE_iheight = (height + IMAGE_shrink) >> IMAGE_shrink;
        IMAGE_iwidth = (width + IMAGE_shrink) >> IMAGE_shrink;
        if ( THE_image.rawData ) {
            GLOBAL_image = (unsigned short (*)[4]) calloc(IMAGE_iheight, IMAGE_iwidth * sizeof *GLOBAL_image);
            memoryError(GLOBAL_image, "main()");
            crop_masked_pixels();
            free(THE_image.rawData);
        }
        if ( zero_is_bad ) {
            remove_zeroes();
        }
        bad_pixels(OPTIONS_values->bpfile);
        if ( OPTIONS_values->dark_frame ) {
            subtract(OPTIONS_values->dark_frame);
        }
        quality = 2 + !fuji_width;
        if ( OPTIONS_values->user_qual >= 0 ) {
            quality = OPTIONS_values->user_qual;
        }
        i = cblack[3];
        for ( c = 0; c < 3; c++ ) {
            if ( i > cblack[c] ) {
                i = cblack[c];
            }
        }
        for ( c = 0; c < 4; c++ ) {
            cblack[c] -= i;
        }
        ADOBE_black += i;
        i = cblack[6];
        for ( c = 0; c < cblack[4] * cblack[5]; c++ ) {
            if ( i > cblack[6 + c] ) {
                i = cblack[6 + c];
            }
        }
        for ( c = 0; c < cblack[4] * cblack[5]; c++ ) {
            cblack[6 + c] -= i;
        }
        ADOBE_black += i;
        if ( OPTIONS_values->user_black >= 0 ) {
            ADOBE_black = OPTIONS_values->user_black;
        }
        for ( c = 0; c < 4; c++ ) {
            cblack[c] += ADOBE_black;
        }
        if ( OPTIONS_values->user_sat > 0 ) {
            ADOBE_maximum = OPTIONS_values->user_sat;
        }
#ifdef COLORCHECK
        colorcheck();
#endif
        if ( is_foveon ) {
            if ( OPTIONS_values->documentMode || TIFF_CALLBACK_loadRawData == &foveon_dp_load_raw ) {
                for ( i = 0; i < height * width * 4; i++ ) {
                    if ((short) GLOBAL_image[0][i] < 0 ) {
                        GLOBAL_image[0][i] = 0;
                    }
                }
            } else {
                foveon_interpolate();
            }
        } else {
            if ( OPTIONS_values->documentMode < 2 ) {
                scale_colors();
            }
        }
        pre_interpolate();
        if ( IMAGE_filters && !OPTIONS_values->documentMode ) {
            if ( quality == 0 ) {
                lin_interpolate();
            } else if ( quality == 1 || IMAGE_colors > 3 ) {
                vng_interpolate();
            } else if ( quality == 2 && IMAGE_filters > 1000 ) {
                ppg_interpolate();
            } else if ( IMAGE_filters == 9 ) {
                xtrans_interpolate(quality * 2 - 3);
            } else {
                ahd_interpolate();
            }
        }
        if ( mix_green ) {
            for ( IMAGE_colors = 3, i = 0; i < height * width; i++ ) {
                GLOBAL_image[i][1] = (GLOBAL_image[i][1] + GLOBAL_image[i][3]) >> 1;
            }
        }
        if ( !is_foveon && IMAGE_colors == 3 ) {
            median_filter();
        }
        if ( !is_foveon && OPTIONS_values->highlight == 2 ) {
            blend_highlights();
        }
        if ( !is_foveon && OPTIONS_values->highlight > 2 ) {
            recover_highlights();
        }
        if ( OPTIONS_values->useFujiRotate ) {
            fuji_rotate();
        }
#ifndef NO_LCMS
        if ( OPTIONS_values->cameraIccProfileFilename ) {
            apply_profile(OPTIONS_values->cameraIccProfileFilename, OPTIONS_values->customOutputProfileForColorSpace);
        }
#endif
        convert_to_rgb();
        if ( OPTIONS_values->useFujiRotate ) {
            stretch();
        }
        thumbnail:

        const char *write_ext;

        if ( write_fun == &jpeg_thumb ) {
            write_ext = ".jpg";
        } else {
            if ( OPTIONS_values->outputTiff && write_fun == &write_ppm_tiff ) {
                write_ext = ".tiff";
            } else {
                int extensionIndex = (int)IMAGE_colors - 1;
                const char *extensions[] = {".pgm", ".ppm", ".ppm", ".pam"};
                write_ext = extensions[extensionIndex];
            }
        }
        ofname = (char *) malloc(strlen(CAMERA_IMAGE_information.inputFilename) + 64);
        memoryError(ofname, "main()");
        if ( OPTIONS_values->write_to_stdout ) {
            strcpy(ofname, _("standard output"));
        } else {
            strcpy(ofname, CAMERA_IMAGE_information.inputFilename);
            if ( (cp = strrchr(ofname, '.')) ) {
                *cp = 0;
            }
            if ( OPTIONS_values->multiOut ) {
                snprintf(ofname + strlen(ofname), strlen(ofname), "_%0*d",
                         snprintf(0, 0, "%d", is_raw - 1), OPTIONS_values->shotSelect);
            }
            if ( OPTIONS_values->thumbnail_only ) {
                strcat(ofname, ".thumb");
            }
            strcat(ofname, write_ext);
            ofp = fopen(ofname, "wb");
            if ( !ofp ) {
                status = 1;
                perror(ofname);
                goto cleanup;
            }
        }
        if ( OPTIONS_values->verbose ) {
            fprintf(stderr, _("Writing data to %s ...\n"), ofname);
        }
        (*write_fun)();
        fclose(GLOBAL_IO_ifp);
        if ( ofp != stdout ) {
            fclose(ofp);
        }
        cleanup:
        if ( meta_data ) {
            free(meta_data);
        }
        if ( ofname ) {
            free(ofname);
        }
        if ( GLOBAL_outputIccProfile ) {
            free(GLOBAL_outputIccProfile);
        }
        if ( GLOBAL_image ) {
            free(GLOBAL_image);
        }
        if ( OPTIONS_values->multiOut ) {
            if ( ++OPTIONS_values->shotSelect < is_raw ) arg--;
            else OPTIONS_values->shotSelect = 0;
        }
    }

    if ( CAMERA_IMAGE_information.inputFilename ) {
        delete CAMERA_IMAGE_information.inputFilename;
        CAMERA_IMAGE_information.inputFilename = nullptr;
    }
    delete OPTIONS_values;
    return status;
}
