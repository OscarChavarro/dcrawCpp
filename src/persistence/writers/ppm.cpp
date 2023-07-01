#ifndef _GNU_SOURCE
    // swab function
    #define _GNU_SOURCE
#endif

#include <unistd.h>

#include "../readers/globalsio.h"
#include "../../common/globals.h"
#include "../../common/mathMacros.h"
#include "../../common/util.h"
#include "../../common/options.h"
#include "../../imageHandling/BayessianImage.h"
#include "../../postprocessors/gamma.h"
#include "../../postprocessors/histogram.h"
#include "ppm.h"
#include "../../common/CameraImageInformation.h"

