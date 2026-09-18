cmake_minimum_required(VERSION 3.22)
project(halo_trial_android C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(ANDROID)
    add_library(hta_native SHARED
        src/engine/engine.c
        src/engine/camera.c
        src/engine/player.c
        src/asset/cache.c
        src/asset/bsp.c
        src/asset/bitmap.c
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
    add_library(hta_engine STATIC src/engine/engine.c src/engine/camera.c src/engine/player.c
        src/asset/cache.c src/asset/bsp.c src/asset/bitmap.c)
    target_include_directories(hta_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(hta_engine PRIVATE -Wall -Wextra)

    enable_testing()
    add_executable(test_engine tests/test_engine.c)
    target_link_libraries(test_engine PRIVATE hta_engine m)
    add_test(NAME engine COMMAND test_engine)

    add_executable(htainfo src/tools/htainfo.c)
    target_link_libraries(htainfo PRIVATE hta_engine m)

    # Host offscreen renderer: needs Vulkan loader + headers on the host.
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        add_library(hta_gfx_host STATIC src/gfx/gfx_vulkan.c src/platform/platform_posix.c)
        target_include_directories(hta_gfx_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
        target_link_libraries(hta_gfx_host PUBLIC ${Vulkan_LIBRARIES} m)
        target_compile_options(hta_gfx_host PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

        add_executable(htaview src/tools/htaview.c)
        target_link_libraries(htaview PRIVATE hta_engine hta_gfx_host m)
        message(STATUS "host Vulkan found: htaview will be built")
    else()
        message(STATUS "host Vulkan NOT found: skipping htaview")
    endif()

    add_executable(test_cache tests/test_cache.c tests/fixture.c)
    target_include_directories(test_cache PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_cache PRIVATE hta_engine m)
    add_test(NAME cache COMMAND test_cache)

    add_executable(test_bsp tests/test_bsp.c tests/fixture.c)
    target_include_directories(test_bsp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_bsp PRIVATE hta_engine m)
    add_test(NAME bsp COMMAND test_bsp)

    add_executable(test_camera tests/test_camera.c)
    target_link_libraries(test_camera PRIVATE hta_engine m)
    add_test(NAME camera COMMAND test_camera)

    add_executable(test_player tests/test_player.c tests/fixture.c)
    target_include_directories(test_player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_player PRIVATE hta_engine m)
    add_test(NAME player COMMAND test_player)

    add_executable(test_bitmap tests/test_bitmap.c)
    target_link_libraries(test_bitmap PRIVATE hta_engine m)
    add_test(NAME bitmap COMMAND test_bitmap)

    add_executable(mkfixture tests/mkfixture.c tests/fixture.c)
    target_include_directories(mkfixture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(mkfixture PRIVATE m)
endif()
