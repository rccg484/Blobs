add_library(bloballoc STATIC BlobAlloc.c)
add_library(BlobAlloc::BlobAlloc ALIAS bloballoc)

target_include_directories(bloballoc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(bloballoc PUBLIC c_std_11)

set_target_properties(bloballoc PROPERTIES
    C_STANDARD          11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS        OFF
)

if(MSVC)
    target_compile_options(bloballoc PRIVATE /W4)
else()
    target_compile_options(bloballoc PRIVATE -Wall -Wextra -Wpedantic)
endif()