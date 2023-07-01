#ifndef __GLOBALS_H__
#define __GLOBALS_H__

// II in ASCII
#define LITTLE_ENDIAN_ORDER 0x4949

// MM in ASCII
#define BIG_ENDIAN_ORDER 0x4D4D

extern short GLOBAL_endianOrder; // LITTLE_ENDIAN_ORDER or BIG_ENDIAN_ORDER

/*
Flips is a set of bits with several flags.
00000001 (0x01): Flip image in X
00000010 (0x02): Flip image in Y
00000100 (0x04): Rotate image by 90 degrees by swapping height and width.
*/
extern unsigned GLOBAL_flipsMask;

// Used only on persistence/readers
extern unsigned GLOBAL_dngVersion;
extern unsigned GLOBAL_loadFlags;

// Used only on imageProcess, pending to handle the per-image initialization
extern unsigned *GLOBAL_outputIccProfile;

// Others ( used everywhere :( )
extern char GLOBAL_bayerPatternLabels[5];

// 0: will map to RGB or custom (raw), non-0: map from sRGB/Adobe/Wide/ProPhoto/XYZ/ACES (non-raw)
extern unsigned GLOBAL_colorTransformForRaw;

extern void (*CALLBACK_loadThumbnailRawData)();

#endif
