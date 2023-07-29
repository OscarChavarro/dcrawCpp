#include "../../../common/globals.h"
#include "../globalsio.h"
#include "imaconRawLoaders.h"

void
imacon_full_load_raw() {
    int row;
    int col;

    if ( !GLOBAL_image ) {
        return;
    }
    for ( row = 0; row < height; row++ ) {
        for ( col = 0; col < width; col++ ) {
            readShorts(GLOBAL_image[row * width + col], 3);
        }
    }
}
