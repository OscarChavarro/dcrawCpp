// C
#include <cstdio>
#include <cfloat>

#ifdef LINUX_PLATFORM
#include <netinet/in.h>
#endif

// App modules
#include "../common/mathMacros.h"
#include "../common/util.h"
#include "../common/options.h"
#include "adobeCoeff.h"
#include "cielab.h"
#include "whiteBalance.h"
#include "../imageHandling/BayessianImage.h"
#include "colorFilters.h"

