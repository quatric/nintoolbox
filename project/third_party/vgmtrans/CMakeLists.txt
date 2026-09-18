cmake_minimum_required(VERSION 3.5)
project(VGMTrans)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DTIXML_USE_STL -DIOAPI_NO_64)

set(INCLUDE_DIRS
    ${PROJECT_SOURCE_DIR}/src/main
    ${PROJECT_SOURCE_DIR}/src/main/formats
    ${PROJECT_SOURCE_DIR}/src/main/loaders
    ${PROJECT_SOURCE_DIR}/src/ui/cli
    ${PROJECT_SOURCE_DIR}/src/ui/qt
    ${PROJECT_SOURCE_DIR}/lib/tinyxml
    ${PROJECT_SOURCE_DIR}/lib/minizip
    ${PROJECT_SOURCE_DIR}/lib/zlib/include
)

include_directories(${INCLUDE_DIRS})

file(GLOB_RECURSE MAIN_SRCS "${PROJECT_SOURCE_DIR}/src/main/*.cpp")
file(GLOB_RECURSE FORMAT_SRCS "${PROJECT_SOURCE_DIR}/src/main/formats/*.cpp")
file(GLOB_RECURSE LOADER_SRCS "${PROJECT_SOURCE_DIR}/src/main/loaders/*.cpp")
file(GLOB TINYXML_SRCS "${PROJECT_SOURCE_DIR}/lib/tinyxml/src/*.cpp")
file(GLOB MINIZIP_SRCS "${PROJECT_SOURCE_DIR}/lib/minizip/*.c")

set(CLI_SRCS
    ${PROJECT_SOURCE_DIR}/src/ui/cli/CLIVGMRoot.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/cli/main.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/qt/osdepend.cpp
)

add_executable(vgmtrans
    ${MAIN_SRCS}
    ${FORMAT_SRCS}
    ${LOADER_SRCS}
    ${TINYXML_SRCS}
    ${MINIZIP_SRCS}
    ${CLI_SRCS}
)

find_package(ZLIB REQUIRED)
target_link_libraries(vgmtrans ${ZLIB_LIBRARIES})

if(WIN32)
    set_target_properties(vgmtrans PROPERTIES LINK_FLAGS "-static-libgcc -static-libstdc++")
endif()
