// C++
#ifndef _GNU_SOURCE
    // strcasestr function
    #define _GNU_SOURCE
#endif

#include <climits>
#include <cstring>

// Unix
#include <ctime>

// External libraries
#include <tiff.h>

// App
#include "../../common/globals.h"
#include "../../common/mathMacros.h"
#include "../../imageHandling/BayessianImage.h"
#include "../../colorRepresentation/adobeCoeff.h"
#include "../../postprocessors/gamma.h"
#include "../../colorRepresentation/cielab.h"
#include "../../colorRepresentation/whiteBalance.h"
#include "../../common/Options.h"
#include "../writers/jpeg.h"
#include "../../common/clearGlobalData.h"

#include "globalsio.h"
#include "../../colorRepresentation/colorFilters.h"
#include "rawloaders/sonyRawLoaders.h"
#include "timestamp.h"
#include "tiffinternal.h"
#include "tiffparser.h"
#include "tiffjpeg.h"

