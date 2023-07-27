#include <arpa/inet.h>
#include <unistd.h>

#include "../../common/globals.h"
#include "globalsio.h"
#include "../../common/CameraImageInformation.h"

FILE *GLOBAL_IO_ifp;
FILE *ofp;
unsigned GLOBAL_IO_dataError;
unsigned GLOBAL_IO_zeroAfterFf;
off_t GLOBAL_IO_profileOffset;

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
unsignedShortEndianSwap(const unsigned char *s) {
    if ( GLOBAL_endianOrder == LITTLE_ENDIAN_ORDER ) {
        /* "II" means little-endian */
        return s[0] | s[1] << 8;
    } else {
        /* "MM" means big-endian */
        return s[0] << 8 | s[1];
    }
}

unsigned
unsignedEndianSwap(const unsigned char *s) {
    if ( GLOBAL_endianOrder == LITTLE_ENDIAN_ORDER ) {
        return s[0] | s[1] << 8 | s[2] << 16 | s[3] << 24;
    } else {
        return s[0] << 24 | s[1] << 16 | s[2] << 8 | s[3];
    }
}

unsigned short
read2bytes() {
    unsigned char str[2] = {0xff, 0xff};
    fread(str, 1, 2, GLOBAL_IO_ifp);
    return unsignedShortEndianSwap(str);
}

unsigned
read4bytes() {
    unsigned char str[4] = {0xff, 0xff, 0xff, 0xff};
    fread(str, 1, 4, GLOBAL_IO_ifp);
    return unsignedEndianSwap(str);
}

unsigned
readInt(int type) {
    return type == 3 ? read2bytes() : read4bytes();
}

double
readDouble(int type) {
    union {
        char c[8];
        double d;
    } u = {'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'};
    int i;
    int rev;

    switch ( type ) {
        case 3:
            return (unsigned short) read2bytes();
        case 4:
            return (unsigned int) read4bytes();
        case 5:
            u.d = (unsigned int) read4bytes();
            return u.d / (unsigned int) read4bytes();
        case 8:
            return (signed short) read2bytes();
        case 9:
            return (signed int) read4bytes();
        case 10:
            u.d = (signed int) read4bytes();
            return u.d / (signed int) read4bytes();
        case 11:
            return intToFloat(read4bytes());
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

unsigned
getbithuff(int nbits, const unsigned short *huff) {
    static unsigned bitbuf = 0;
    static int vbits = 0;
    static int reset = 0;
    unsigned c;

    if ( nbits > 25 ) {
        return 0;
    }
    if ( nbits < 0 ) {
        return bitbuf = vbits = reset = 0;
    }
    if ( nbits == 0 || vbits < 0 ) {
        return 0;
    }
    while ( !reset && vbits < nbits && (c = fgetc(GLOBAL_IO_ifp)) != EOF &&
            !(reset = GLOBAL_IO_zeroAfterFf && c == 0xff && fgetc(GLOBAL_IO_ifp)) ) {
        bitbuf = (bitbuf << 8) + (unsigned char) c;
        vbits += 8;
    }
    c = bitbuf << (32 - vbits) >> (32 - nbits);
    if ( huff ) {
        vbits -= huff[c] >> 8;
        c = (unsigned char) huff[c];
    } else {
        vbits -= nbits;
    }
    if ( vbits < 0 ) {
        inputOutputError();
    }
    return c;
}

unsigned
ph1_bithuff(int nbits, unsigned short *huff) {
    static unsigned long long bitbuf = 0;
    static int vbits = 0;
    unsigned c;

    if ( nbits == -1 ) {
        return bitbuf = vbits = 0;
    }
    if ( nbits == 0 ) {
        return 0;
    }
    if ( vbits < nbits ) {
        bitbuf = bitbuf << 32 | read4bytes();
        vbits += 32;
    }
    c = bitbuf << (64 - vbits) >> (64 - nbits);
    if ( huff ) {
        vbits -= huff[c] >> 8;
        return (unsigned char)huff[c];
    }
    vbits -= nbits;
    return c;
}

