cmake_minimum_required(VERSION 3.10.2)

project(dcraw)

add_executable(dcraw
        src/colorRepresentation/ColorRgb.cpp
        src/colorRepresentation/ColorRgb.h
        src/colorRepresentation/PixelRgb16Bits.cpp
        src/colorRepresentation/PixelRgb16Bits.h
        src/colorRepresentation/adobeCoeff.cpp
        src/colorRepresentation/adobeCoeff.h
        src/colorRepresentation/cielab.cpp
        src/colorRepresentation/cielab.h
        src/colorRepresentation/colorFilters.cpp
        src/colorRepresentation/colorFilters.h
        src/colorRepresentation/colorSpaceMatrices.cpp
        src/colorRepresentation/colorSpaceMatrices.h
        src/colorRepresentation/iccProfile.cpp
        src/colorRepresentation/iccProfile.h
        src/colorRepresentation/whiteBalance.cpp
        src/colorRepresentation/whiteBalance.h
        src/common/CameraImageInformation.cpp
        src/common/CameraImageInformation.h
        src/common/clearGlobalData.cpp
        src/common/clearGlobalData.h
        src/common/globals.cpp
        src/common/globals.h
        src/common/mathMacros.h
        src/common/Options.cpp
        src/common/Options.h
        src/common/util.cpp
        src/common/util.h
        src/dcrawMain.cpp
        src/imageHandling/BayessianImage.cpp
        src/imageHandling/BayessianImage.h
        src/imageHandling/rawAnalysis.cpp
        src/imageHandling/rawAnalysis.h
        src/imageProcess.cpp
        src/imageProcess.h
        src/interpolation/AhdInterpolator.cpp
        src/interpolation/AhdInterpolator.h
        src/interpolation/BiLinearInterpolator.cpp
        src/interpolation/BiLinearInterpolator.h
        src/interpolation/Interpolator.cpp
        src/interpolation/Interpolator.h
        src/interpolation/PpgInterpolator.cpp
        src/interpolation/PpgInterpolator.h
        src/interpolation/VgnInterpolator.cpp
        src/interpolation/VgnInterpolator.h
        src/persistence/readers/globalsio.cpp
        src/persistence/readers/globalsio.h
        src/persistence/readers/rawloaders/sonyRawLoaders.cpp
        src/persistence/readers/rawloaders/sonyRawLoaders.h
        src/persistence/readers/tiffinternal.cpp
        src/persistence/readers/tiffinternal.h
        src/persistence/readers/tiffjpeg.cpp
        src/persistence/readers/tiffjpeg.h
        src/persistence/readers/tiffparser.cpp
        src/persistence/readers/tiffparser.h
        src/persistence/readers/timestamp.cpp
        src/persistence/readers/timestamp.h
        src/persistence/writers/jpeg.cpp
        src/persistence/writers/jpeg.h
        src/persistence/writers/ppm.cpp
        src/persistence/writers/ppm.h
        src/postprocessors/gamma.cpp
        src/postprocessors/gamma.h
        src/postprocessors/histogram.cpp
        src/postprocessors/histogram.h
        src/thumbnailExport.cpp
        src/thumbnailExport.h src/persistence/readers/rawloaders/jpegRawLoaders.cpp src/persistence/readers/rawloaders/jpegRawLoaders.h src/persistence/readers/rawloaders/nikonRawLoaders.cpp src/persistence/readers/rawloaders/nikonRawLoaders.h src/persistence/readers/rawloaders/hasselbladRawLoaders.cpp src/persistence/readers/rawloaders/hasselbladRawLoaders.h src/persistence/readers/rawloaders/canonRawLoaders.cpp src/persistence/readers/rawloaders/canonRawLoaders.h)
target_link_libraries(dcraw PRIVATE jasper jpeg tiff lcms2)

if(UNIX AND NOT APPLE)
    set(CMAKE_CXX_STANDARD 98)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    add_definitions(-DLINUX_PLATFORM)
endif()

if(APPLE)
    include_directories(/opt/homebrew/include)
    target_link_directories(dcraw PRIVATE /opt/homebrew/lib)
    add_definitions(-DMACOS_PLATFORM)
endif()

if(CMAKE_BUILD_TYPE MATCHES "Release")
    add_custom_command(
        TARGET dcraw POST_BUILD
        COMMAND strip $<TARGET_FILE:dcraw>
    )
endif()
