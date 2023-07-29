#include "globals.h"

short GLOBAL_endianOrder;
unsigned GLOBAL_flipsMask;
unsigned GLOBAL_dngVersion;
unsigned GLOBAL_loadFlags;
unsigned *GLOBAL_outputIccProfile;
char GLOBAL_bayerPatternLabels[5];
unsigned GLOBAL_colorTransformForRaw;
void (*CALLBACK_loadThumbnailRawData)();

unsigned short (*GLOBAL_image)[4];
char GLOBAL_make[64];
char GLOBAL_model[64];
off_t GLOBAL_meta_offset;
float GLOBAL_cam_mul[4];

unsigned short cblack[4102];
unsigned tiff_samples;
unsigned short top_margin;
unsigned short left_margin;
unsigned mix_green;
float pre_mul[4];
float flash_used;
float rgb_cam[3][4];
float canon_ev;
unsigned tiff_compress;
unsigned short cr2_slice[3];
off_t strip_offset;
unsigned is_raw;
unsigned tile_width;
unsigned tile_length;
unsigned thumb_misc;
unsigned short height;
unsigned short width;
char model2[64];
unsigned unique_id;
unsigned short sraw_mul[4];
unsigned thumb_length;
unsigned short thumb_width;
unsigned short thumb_height;
unsigned meta_length;
unsigned kodak_cbpp;
jmp_buf failure;

struct ph1 ph1;
