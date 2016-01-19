INCLUDE(FindPkgConfig)
PKG_CHECK_MODULES(PC_LZ4 "liblz4")

FIND_PATH(LZ4_INCLUDE_DIRS
    NAMES lz4.h
    HINTS ${PC_LZ4_INCLUDE_DIR}
    ${CMAKE_INSTALL_PREFIX}/include
    PATHS
    /usr/local/include
    /usr/include
)

FIND_LIBRARY(LZ4_LIBRARIES
    NAMES lz4 liblz4
    HINTS ${PC_LZ4_LIBDIR}
    ${CMAKE_INSTALL_PREFIX}/lib
    ${CMAKE_INSTALL_PREFIX}/lib64
    PATHS
    ${ZEROMQ_INCLUDE_DIRS}/../lib
    /usr/local/lib
    /usr/lib
)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LZ4 DEFAULT_MSG LZ4_LIBRARIES LZ4_INCLUDE_DIRS)
MARK_AS_ADVANCED(LZ4_LIBRARIES LZ4_INCLUDE_DIRS)
