#ifndef __GLOBALS_H__
#define __GLOBALS_H__

#include <cstdio>
#include <csetjmp>

// II in ASCII
#define LITTLE_ENDIAN_ORDER 0x4949

// MM in ASCII
#define BIG_ENDIAN_ORDER 0x4D4D

extern short GLOBAL_endianOrder; // LITTLE_ENDIAN_ORDER or BIG_ENDIAN_ORDER

/*
Flips is a set of bits with several flags.
00000001 (0x01): Flip GLOBAL_image in X
00000010 (0x02): Flip GLOBAL_image in Y
00000100 (0x04): Rotate GLOBAL_image by 90 degrees by swapping height and width.
*/
extern unsigned GLOBAL_flipsMask;

// Used only on persistence/readers
extern unsigned GLOBAL_dngVersion;
extern unsigned GLOBAL_loadFlags;

// Used only on imageProcess, pending to handle the per-GLOBAL_image initialization
extern unsigned *GLOBAL_outputIccProfile;

// Others ( used everywhere :( )
extern char GLOBAL_bayerPatternLabels[5];

// 0: will map to RGB or custom (raw), non-0: map from sRGB/Adobe/Wide/ProPhoto/XYZ/ACES (non-raw)
extern unsigned GLOBAL_colorTransformForRaw;

// Others
extern void (*CALLBACK_loadThumbnailRawData)();

extern unsigned short (*GLOBAL_image)[4];
extern char GLOBAL_make[64];
extern char GLOBAL_model[64];
extern off_t GLOBAL_meta_offset;
extern float GLOBAL_cam_mul[4];

extern unsigned short cblack[4102];
extern unsigned tiff_samples;
extern unsigned short top_margin;
extern unsigned short left_margin;
extern unsigned mix_green;
extern float pre_mul[4];
extern float flash_used;
extern float rgb_cam[3][4];
extern float canon_ev;
extern unsigned tiff_compress;
extern unsigned short cr2_slice[3];
extern off_t strip_offset;
extern unsigned is_raw;
extern unsigned tile_width;
extern unsigned tile_length;
extern unsigned thumb_misc;
extern unsigned short height;
extern unsigned short width;
extern char model2[64];
extern unsigned unique_id;
extern unsigned short sraw_mul[4];
extern unsigned thumb_length;
extern unsigned short thumb_width;
extern unsigned short thumb_height;
extern unsigned meta_length;
extern jmp_buf failure;

struct ph1 {
    int format;
    int key_off;
    int tag_21a;
    int black;
    int split_col;
    int black_col;
    int split_row;
    int black_row;
    float tag_210;
};

extern struct ph1 ph1;

#ifdef LOCALEDIR
#include <libintl.h>
#define _(String) gettext(String)
#else
#define _(String) (String)
#endif

#endif
