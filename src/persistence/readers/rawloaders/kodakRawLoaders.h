#ifndef __KODAKRAWLOADERS__
#define __KODAKRAWLOADERS__

extern void kodak_c330_load_raw();
extern void kodak_c603_load_raw();
extern void kodak_262_load_raw();
extern int kodak_65000_decode(short *out, int bsize);
extern void kodak_65000_load_raw();
extern void kodak_ycbcr_load_raw();
extern void kodak_rgb_load_raw();
extern void kodak_thumb_load_raw();

#endif