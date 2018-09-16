cmake_minimum_required(VERSION 3.5)
project(distrend)

# We need protobuf.
find_package(Protobuf REQUIRED)

# Normal flags.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wfatal-errors -Wall -Wextra -Wpedantic -Wshadow -O3 -ffast-math")

# Unfortunately protobuf's generated C++ code is pretty crappy, so we have to turn
# off a bunch of warnings globally.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nested-anon-types -Wno-unused-parameter -Wno-deprecated-declarations -Wno-sign-compare")

# Our source files.
file(GLOB SOURCES "*.cpp")

# Generate protobuf files.
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS Drp.proto)
set(SOURCES ${SOURCES} ${PROTO_SRCS})

# Our binary.
add_executable(distrend ${SOURCES})

# What to link with.
target_link_libraries(distrend pthread ${PROTOBUF_LIBRARIES})

# We need these C++ features.
target_compile_features(distrend PRIVATE cxx_long_long_type)

# Look for the standard protobuf headers, as well as the one we generated.
target_include_directories(distrend PRIVATE
    ${PROTOBUF_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR})

