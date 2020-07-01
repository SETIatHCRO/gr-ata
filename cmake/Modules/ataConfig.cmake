INCLUDE(FindPkgConfig)
PKG_CHECK_MODULES(PC_ATA ata)

FIND_PATH(
    ATA_INCLUDE_DIRS
    NAMES ata/api.h
    HINTS $ENV{ATA_DIR}/include
        ${PC_ATA_INCLUDEDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/include
          /usr/local/include
          /usr/include
)

FIND_LIBRARY(
    ATA_LIBRARIES
    NAMES gnuradio-ata
    HINTS $ENV{ATA_DIR}/lib
        ${PC_ATA_LIBDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/lib
          ${CMAKE_INSTALL_PREFIX}/lib64
          /usr/local/lib
          /usr/local/lib64
          /usr/lib
          /usr/lib64
          )

include("${CMAKE_CURRENT_LIST_DIR}/ataTarget.cmake")

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(ATA DEFAULT_MSG ATA_LIBRARIES ATA_INCLUDE_DIRS)
MARK_AS_ADVANCED(ATA_LIBRARIES ATA_INCLUDE_DIRS)
