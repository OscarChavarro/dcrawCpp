cmake_minimum_required(VERSION 3.10.2)

project(dcraw)

add_executable(dcraw
    src/dcrawMain.cpp
)
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
