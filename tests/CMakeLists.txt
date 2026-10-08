include(FetchContent)

FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.5.2
    SOURCE_SUBDIR  nonexistent
)
FetchContent_MakeAvailable(unity)

add_library(unity_lib STATIC ${unity_SOURCE_DIR}/src/unity.c)
target_include_directories(unity_lib PUBLIC ${unity_SOURCE_DIR}/src)

add_library(test_common STATIC test_common.c)
target_link_libraries(test_common PUBLIC bloballoc blobbox blobvec unity_lib)
target_include_directories(test_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_bloballoc test_bloballoc.c)
target_link_libraries(test_bloballoc PRIVATE test_common unity_lib)
add_test(NAME bloballoc COMMAND test_bloballoc)

add_executable(test_blobbox test_blobbox.c)
target_link_libraries(test_blobbox PRIVATE test_common blobbox unity_lib)
add_test(NAME blobbox COMMAND test_blobbox)

add_executable(test_blobvec test_blobvec.c)
target_link_libraries(test_blobvec PRIVATE test_common blobvec unity_lib)
add_test(NAME blobvec COMMAND test_blobvec)

add_executable(test_integration test_integration.c)
target_link_libraries(test_integration PRIVATE test_common blobbox blobvec unity_lib)
add_test(NAME integration COMMAND test_integration)

# On Windows, clang's ASan runtime is a DLL that ships next to clang.exe
# and is never on PATH. Copy it next to the test executables so they can
# be run directly (not just through CTest) without environment setup.
if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
    get_filename_component(_clang_bin "${CMAKE_C_COMPILER}" DIRECTORY)

    set(_sanitizer_dlls
        "${_clang_bin}/clang_rt.asan_dynamic-x86_64.dll"
        "${_clang_bin}/clang_rt.ubsan_standalone-x86_64.dll"
    )

    foreach(_tgt test_bloballoc test_blobbox test_blobvec test_integration)
        foreach(_dll ${_sanitizer_dlls})
            if(EXISTS "${_dll}")
                add_custom_command(TARGET ${_tgt} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_dll}" "$<TARGET_FILE_DIR:${_tgt}>"
                    COMMENT "Copying $(basename ${_dll}) next to ${_tgt}"
                )
            endif()
        endforeach()
    endforeach()
endif()