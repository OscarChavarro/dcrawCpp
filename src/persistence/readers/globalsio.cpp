#include <arpa/inet.h>
#include <unistd.h>

#include "../../common/globals.h"
#include "globalsio.h"
#include "../../common/CameraImageInformation.h"

FILE *GLOBAL_IO_ifp;
unsigned GLOBAL_IO_dataError;

void
inputOutputError() {
    if ( !GLOBAL_IO_dataError ) {
        fprintf(stderr, "%s: ", CAMERA_IMAGE_information.inputFilename);
        if ( feof(GLOBAL_IO_ifp) ) {
            fprintf(stderr, "Unexpected end of file\n");
        } else {
            fprintf(stderr, "Corrupt data near 0x%llx\n", (long long) ftello(GLOBAL_IO_ifp));
        }
    }
    GLOBAL_IO_dataError++;
}

float
intToFloat(int i) {
    union {
        int i;
        float f;
    } u = {i};
    u.i = i;
    return u.f;
}

unsigned short
sget2(const unsigned char *s) {
    if ( GLOBAL_endianOrder == LITTLE_ENDIAN_ORDER ) {
        /* "II" means little-endian */
        return s[0] | s[1] << 8;
    } else {
        /* "MM" means big-endian */
        return s[0] << 8 | s[1];
    }
}

unsigned
unsignedShortEndianSwap(unsigned char *s) {
    if ( GLOBAL_endianOrder == LITTLE_ENDIAN_ORDER ) {
        return s[0] | s[1] << 8 | s[2] << 16 | s[3] << 24;
    } else {
        return s[0] << 24 | s[1] << 16 | s[2] << 8 | s[3];
    }
}

unsigned short
get2() {
    unsigned char str[2] = {0xff, 0xff};
    fread(str, 1, 2, GLOBAL_IO_ifp);
    return sget2(str);
}

unsigned
get4() {
    unsigned char str[4] = {0xff, 0xff, 0xff, 0xff};
    fread(str, 1, 4, GLOBAL_IO_ifp);
    return unsignedShortEndianSwap(str);
}

unsigned
getInt(int type) {
    return type == 3 ? get2() : get4();
}

double
getReal(int type) {
    union {
        char c[8];
        double d;
    } u;
    int i;
    int rev;

    switch ( type ) {
        case 3:
            return (unsigned short) get2();
        case 4:
            return (unsigned int) get4();
        case 5:
            u.d = (unsigned int) get4();
            return u.d / (unsigned int) get4();
        case 8:
            return (signed short) get2();
        case 9:
            return (signed int) get4();
        case 10:
            u.d = (signed int) get4();
            return u.d / (signed int) get4();
        case 11:
            return intToFloat(get4());
        case 12:
            rev = 7 * ((GLOBAL_endianOrder == LITTLE_ENDIAN_ORDER) == (ntohs(0x1234) == 0x1234));
            for ( i = 0; i < 8; i++ ) {
                u.c[i ^ rev] = fgetc(GLOBAL_IO_ifp);
            }
            return u.d;
        default:
            return fgetc(GLOBAL_IO_ifp);
    }
}

void
readShorts(unsigned short *pixel, int count) {
    if ( fread(pixel, 2, count, GLOBAL_IO_ifp) < count ) {
        inputOutputError();
    }
    if ( (GLOBAL_endianOrder == LITTLE_ENDIAN_ORDER) == (ntohs(0x1234) == 0x1234) ) {
        swab(pixel, pixel, count * 2);
    }
}

