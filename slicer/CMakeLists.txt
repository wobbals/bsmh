cmake_minimum_required(VERSION 3.5)

project(slicer)

file(GLOB SOURCES "src/*.c")

find_package(PkgConfig)

pkg_check_modules(LIBGSTREAMER REQUIRED gstreamer-1.0)

link_libraries(
  ${LIBGSTREAMER_LDFLAGS}
)

include_directories(
  ${LIBGSTREAMER_INCLUDE_DIRS}
)

add_executable(slicer ${SOURCES})
