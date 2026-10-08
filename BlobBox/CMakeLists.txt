add_library(blobbox STATIC BlobBox.c)
add_library(BlobBox::BlobBox ALIAS blobbox)

target_include_directories(blobbox PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(blobbox PUBLIC BlobAlloc::BlobAlloc)
target_compile_features(blobbox PUBLIC c_std_11)

set_target_properties(blobbox PROPERTIES
    C_STANDARD          11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS        OFF
)

if(MSVC)
    target_compile_options(blobbox PRIVATE /W4)
else()
    target_compile_options(blobbox PRIVATE -Wall -Wextra -Wpedantic)
endif()