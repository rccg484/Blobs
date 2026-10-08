cmake_minimum_required(VERSION 3.20)
project(Blobs C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

add_subdirectory(BlobAlloc)
add_subdirectory(BlobBox)
add_subdirectory(BlobVec)
add_subdirectory(tests)