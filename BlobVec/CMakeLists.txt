add_library(blobvec STATIC BlobVec.c)
add_library(BlobVec::BlobVec ALIAS blobvec)

target_include_directories(blobvec PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(blobvec PUBLIC BlobAlloc::BlobAlloc)
target_compile_features(blobvec PUBLIC c_std_11)

set_target_properties(blobvec PROPERTIES
    C_STANDARD          11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS        OFF
)

if(MSVC)
    target_compile_options(blobvec PRIVATE /W4)
else()
    target_compile_options(blobvec PRIVATE -Wall -Wextra -Wpedantic)
endif()