cmake_minimum_required(VERSION 3.22)
project(halo_trial_android C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(ANDROID)
    add_library(hta_native SHARED
        src/engine/engine.c
        src/gfx/gfx_vulkan.c
        src/platform/platform_android.c
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
    )
    target_include_directories(hta_native PRIVATE
        ${ANDROID_NDK}/sources/android/native_app_glue
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(hta_native PRIVATE android log vulkan m)
    # Vulkan structs are idiomatically brace-initialised with only sType set.
    target_compile_options(hta_native PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    # native_app_glue registers android_main via a constructor; keep it.
    target_link_options(hta_native PRIVATE -u ANativeActivity_onCreate)
else()
    # Host build: the portable engine only, plus its tests.
    add_library(hta_engine STATIC src/engine/engine.c)
    target_include_directories(hta_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(hta_engine PRIVATE -Wall -Wextra)

    enable_testing()
    add_executable(test_engine tests/test_engine.c)
    target_link_libraries(test_engine PRIVATE hta_engine m)
    add_test(NAME engine COMMAND test_engine)
endif()
