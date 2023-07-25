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
