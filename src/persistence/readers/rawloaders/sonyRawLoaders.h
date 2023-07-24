#ifndef __SONY_RAW_LOADERS__
#define __SONY_RAW_LOADERS__

extern void sony_decrypt(unsigned *data, int len, int start, int key);
extern void sony_load_raw();
extern void sony_arw_load_raw();
extern void sony_arw2_load_raw();

#endif
