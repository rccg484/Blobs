add_library(bloblist STATIC BlobList.c)
add_library(BlobList::BlobList ALIAS bloblist)

target_include_directories(bloblist PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(bloblist PUBLIC BlobAlloc::BlobAlloc)
target_compile_features(bloblist PUBLIC c_std_11)

set_target_properties(bloblist PROPERTIES
    C_STANDARD          11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS        OFF
)

if(MSVC)
    target_compile_options(bloblist PRIVATE /W4)
else()
    target_compile_options(bloblist PRIVATE -Wall -Wextra -Wpedantic)
endif()