# dcrawCpp
A modernized CPP version of [DCRAW](https://www.dechifro.org/dcraw/) image loading software from Dave Coffin.

## Why to have this fork?

Dcraw is a fantastic software piece available since ancient times (February 1997), thanks to Dave Coffin. It is
common on several distributions, as such GNU/Linux, MacOS (using brew) and more. It seems that original author
is no longer maintaining this software, so this repository contains a "modernized" version with the following
characteristics:
- Cmake build
- Legacy C code ported to C++
- Original code were on a huge (10K) lines of code .C source file, that has been splitted on several C++ modules
- Small quality / compatibility improvements to support clean builds on new compilers (i.e. build with no errors and no warnings)
- Code refactored for better readability and maintainability (variable and functions renamed).
- Clarification comments added when useful.
- Testing data being added (current example images from several different cameras)

It is expected that after upgrading basic code, new fixes and features are easier to do.

## Dependencies

To build this program the following software elements are needed:
- Essential build tools (make, a C++ compiler as such gcc or XCode, a linker)
- Cmake
- jpeg library
- jasper library
- tiff library
- lcms2 library

## Build

Common command line build:

``` 
mkdir build
cd build
cmake ..
make
./dcraw
```

author of this branch uses CLion. This is sadly not open source, but it contains a great set of tools. On this
and other IDEs, Cmake project can easily imported to handle both Release and Debug configurations.

## Run

The `dcraw` command with no arguments will show a set of options to run. An example simple run command will look
like this:

```
./bin/dcraw -v share/sampleImages/Sony/ILCE-7RM4/14bitsUncompressed.arw
```

in this example, given raw image taken from a mirrorless camera will be converted to a simple .ppm file
that will be located in the same folder as the source. Note that in the conversion process, there are several
steps involved:
- Original raw files from cameras are usually binary variants of TIFF file format specification: container files with several different pieces of information inside: multiple images (i.e. raw image from sensor in Bayer tile layout + preview images of several sizes, GPS information, camera exif metadata, etc.)
- Since sensor data comes in some sort of tiling, program needs to do a de-mosaic process, involving an interpolation step and a color transformation.
- Program converts the image data from several available color spaces in to RGB.
- Program modifies the image, using color aberration, white balance and gamma function corrections.
- Program writes final image in to output PPM/PNG file.
