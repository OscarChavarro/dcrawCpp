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
