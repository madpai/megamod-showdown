cmake_minimum_required(VERSION 3.22)
project(halo_trial_android C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(ANDROID)
    add_library(hta_native SHARED
        src/net/protocol.c
        src/net/replication.c
        src/net/udp.c
        src/net/session.c
        src/engine/engine.c
        src/engine/camera.c
        src/engine/player.c
        src/engine/vehicle.c
        src/engine/gun.c
        src/engine/projectile.c
        src/engine/particle.c
        src/engine/contrail.c
        src/engine/shake.c
        src/engine/vitals.c
        src/engine/actor.c
        src/engine/pickup.c
        src/engine/bot.c
        src/engine/viewmodel.c
        src/engine/audio.c
        src/engine/ammo.c
        src/engine/hud.c
        src/engine/rigid.c
        src/engine/fx.c
        src/engine/gore.c
        src/engine/props.c
        src/engine/weather.c
        src/engine/sfx.c
        src/app/report.c
        src/game/nav.c
        src/game/external_world.c
        src/game/imported.c
        src/game/world_fx.c
        src/game/world_fx_audio.c
        src/game/world_fx_gpu.c
        src/game/game.c
        src/game/brain.c
        src/game/view.c
        src/game/menu.c
        src/asset/cache.c
        src/asset/bsp.c
        src/asset/external_map.c src/asset/oal_asset.c
        src/asset/bitmap.c
        src/asset/sound.c
        src/asset/ogg.c
        src/asset/font.c
        src/asset/effect.c
        src/asset/dialogue.c
        src/asset/strings.c
        src/asset/items.c
        src/asset/biped.c
        src/asset/weapon.c
        src/asset/anim.c
        src/asset/model.c
        src/gfx/gfx_settings.c
        src/gfx/gfx_vulkan.c
        src/platform/platform_android.c
        src/platform/audio_android.c
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
    )
    target_include_directories(hta_native PRIVATE
        ${ANDROID_NDK}/sources/android/native_app_glue
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(hta_native PRIVATE android log vulkan aaudio m)
    # Vulkan structs are idiomatically brace-initialised with only sType set.
    target_compile_options(hta_native PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    # native_app_glue registers android_main via a constructor; keep it.
    target_link_options(hta_native PRIVATE -u ANativeActivity_onCreate)
else()
    add_library(hta_net STATIC src/net/protocol.c src/net/replication.c src/net/udp.c src/net/session.c)
    target_include_directories(hta_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(hta_net PRIVATE -Wall -Wextra)
    # Host build: the portable engine only, plus its tests.
    add_library(hta_engine STATIC src/engine/engine.c src/engine/camera.c src/engine/player.c src/engine/vehicle.c src/engine/gun.c
        src/engine/projectile.c
        src/engine/particle.c
        src/engine/contrail.c
        src/engine/shake.c
        src/engine/vitals.c
        src/engine/actor.c
        src/engine/pickup.c
        src/engine/bot.c src/engine/viewmodel.c src/engine/audio.c src/engine/ammo.c src/engine/hud.c
        src/engine/rigid.c src/engine/fx.c src/engine/gore.c src/engine/props.c src/engine/weather.c src/engine/sfx.c
        src/game/nav.c
        src/game/external_world.c
        src/game/imported.c
        src/game/world_fx.c src/game/world_fx_audio.c
        src/game/game.c
        src/game/brain.c
        src/game/view.c
        src/game/menu.c
        src/asset/cache.c src/asset/bsp.c src/asset/external_map.c src/asset/oal_asset.c src/asset/bitmap.c src/asset/sound.c src/asset/ogg.c src/asset/effect.c src/asset/dialogue.c src/asset/strings.c src/asset/items.c src/asset/biped.c src/asset/weapon.c
        src/asset/anim.c src/asset/model.c src/asset/font.c
        src/gfx/gfx_settings.c)
    target_include_directories(hta_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(hta_engine PRIVATE -Wall -Wextra)

    enable_testing()
    add_executable(test_external_map tests/test_external_map.c)
    target_link_libraries(test_external_map PRIVATE hta_engine m)
    add_test(NAME external_map COMMAND test_external_map)
    add_executable(test_net tests/test_net.c)
    target_link_libraries(test_net PRIVATE hta_net m)
    add_test(NAME net COMMAND test_net)
    # A LAN joiner's view of a host's match: the PC client's core.
    add_library(hta_netview STATIC src/game/net_view.c)
    target_include_directories(hta_netview PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(hta_netview PUBLIC hta_engine hta_net m)
    target_compile_options(hta_netview PRIVATE -Wall -Wextra)
    # A scripted LAN host (bots, gibbed kills, blasts, props) to test joiners.
    add_executable(megamod-fakehost src/tools/fakehost.c)
    target_link_libraries(megamod-fakehost PRIVATE hta_net m)
    add_executable(test_net_view tests/test_net_view.c)
    target_link_libraries(test_net_view PRIVATE hta_netview)
    add_test(NAME net_view COMMAND test_net_view)
    add_executable(test_net_fuzz tests/test_net_fuzz.c)
    target_link_libraries(test_net_fuzz PRIVATE hta_net m)
    add_test(NAME net_fuzz COMMAND test_net_fuzz)
    add_executable(htanet src/tools/htanet.c)
    target_link_libraries(htanet PRIVATE hta_net m)
    add_executable(test_engine tests/test_engine.c)
    target_link_libraries(test_engine PRIVATE hta_engine m)
    add_test(NAME engine COMMAND test_engine)

    add_executable(htainfo src/tools/htainfo.c)
    target_link_libraries(htainfo PRIVATE hta_engine m)

    # Measures the pawn's fit at a reported coordinate: what pushes, and why.
    add_executable(htaprobe src/tools/htaprobe.c)
    target_link_libraries(htaprobe PRIVATE hta_engine m)

    # What format the Trial's samples are in, and where the bytes live.
    add_executable(htasound src/tools/htasound.c)
    target_link_libraries(htasound PRIVATE hta_engine m)

    # Host offscreen renderer: needs Vulkan loader + headers on the host.
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        add_library(hta_gfx_host STATIC src/gfx/gfx_vulkan.c src/gfx/gfx_settings.c src/game/world_fx_gpu.c src/platform/platform_posix.c)
        target_include_directories(hta_gfx_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIRS})
        target_link_libraries(hta_gfx_host PUBLIC ${Vulkan_LIBRARIES} m)
        target_compile_options(hta_gfx_host PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

        add_executable(htamenu src/tools/htamenu.c)
        target_link_libraries(htamenu PRIVATE hta_engine hta_gfx_host m)
        target_compile_options(htamenu PRIVATE -Wall -Wextra)

        add_executable(htamatch src/tools/htamatch.c)
        target_link_libraries(htamatch PRIVATE hta_engine hta_gfx_host m)
        target_compile_options(htamatch PRIVATE -Wall -Wextra)

        add_executable(open-halo-map-test src/tools/open_halo_map_test.c)
        target_link_libraries(open-halo-map-test PRIVATE hta_engine hta_gfx_host m)
        add_executable(open-halo-asset-test src/tools/open_halo_asset_test.c)
        target_link_libraries(open-halo-asset-test PRIVATE hta_engine hta_gfx_host m)

        # The renderer's own pictures, on a synthetic scene. Skips (77)
        # when there is no Vulkan device; lavapipe is enough.
        add_executable(test_gfx_render tests/test_gfx_render.c)
        target_link_libraries(test_gfx_render PRIVATE hta_gfx_host hta_engine m)
        add_test(NAME gfx_render COMMAND test_gfx_render)
        set_tests_properties(gfx_render PROPERTIES SKIP_RETURN_CODE 77)

        add_executable(htaview src/tools/htaview.c)
        target_link_libraries(htaview PRIVATE hta_engine hta_gfx_host m)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
            if(SDL2_FOUND)
                # The PC joins a LAN match (a phone's, or megamod-fakehost): protocol v9.
                add_executable(megamod-join src/tools/join.c)
                target_link_libraries(megamod-join PRIVATE hta_desktop hta_netview hta_engine hta_gfx_host hta_net PkgConfig::SDL2 m)
                target_compile_options(megamod-join PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
                add_executable(htaplay src/tools/htaplay.c)
                target_link_libraries(htaplay PRIVATE hta_desktop hta_engine hta_gfx_host hta_net PkgConfig::SDL2 m)
                target_compile_options(htaplay PRIVATE -Wall -Wextra)
                message(STATUS "host SDL2 found: htaplay will be built")
                # The desktop platform layer (window, Vulkan surface, input,
                # pacing) and the data-free sandbox on top of it.
                add_library(hta_desktop STATIC src/platform/desktop_sdl.c src/platform/audio_sdl.c)
                target_link_libraries(hta_desktop PUBLIC hta_gfx_host hta_engine PkgConfig::SDL2)
                target_compile_options(hta_desktop PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
                add_executable(megamod-sandbox src/tools/sandbox.c)
                target_link_libraries(megamod-sandbox PRIVATE hta_desktop hta_gfx_host hta_engine m)
                target_compile_options(megamod-sandbox PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
            endif()
        endif()
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

    add_executable(test_vehicle tests/test_vehicle.c)
    target_link_libraries(test_vehicle PRIVATE hta_engine m)
    add_test(NAME vehicle COMMAND test_vehicle)

    add_executable(test_model tests/test_model.c)
    target_link_libraries(test_model PRIVATE hta_engine m)
    add_test(NAME model COMMAND test_model)

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

    add_executable(test_biped tests/test_biped.c)
    target_link_libraries(test_biped PRIVATE hta_engine m)
    add_test(NAME biped COMMAND test_biped)

    add_executable(test_anim tests/test_anim.c)
    target_link_libraries(test_anim PRIVATE hta_engine m)
    add_test(NAME anim COMMAND test_anim)

    add_executable(test_vitals tests/test_vitals.c)
    target_link_libraries(test_vitals PRIVATE hta_engine m)
    add_test(NAME vitals COMMAND test_vitals)

    add_executable(test_actor tests/test_actor.c)
    target_link_libraries(test_actor PRIVATE hta_engine m)
    add_test(NAME actor COMMAND test_actor)

    add_executable(test_pickup tests/test_pickup.c)
    target_link_libraries(test_pickup PRIVATE hta_engine m)
    add_test(NAME pickup COMMAND test_pickup)

    add_executable(test_oal_asset tests/test_oal_asset.c)
    target_link_libraries(test_oal_asset PRIVATE hta_engine m)
    add_test(NAME oal_asset COMMAND test_oal_asset)
    add_executable(test_external_world tests/test_external_world.c)
    target_link_libraries(test_external_world PRIVATE hta_engine m)
    add_test(NAME external_world COMMAND test_external_world)
    add_executable(test_nav tests/test_nav.c)
    target_link_libraries(test_nav PRIVATE hta_engine m)
    add_test(NAME nav COMMAND test_nav)

    add_executable(test_game tests/test_game.c)
    target_link_libraries(test_game PRIVATE hta_engine m)
    add_test(NAME game COMMAND test_game)

    add_executable(test_contrail tests/test_contrail.c)
    target_link_libraries(test_contrail PRIVATE hta_engine m)
    add_test(NAME contrail COMMAND test_contrail)

    add_executable(test_shake tests/test_shake.c)
    target_link_libraries(test_shake PRIVATE hta_engine m)
    add_test(NAME shake COMMAND test_shake)

    add_executable(test_ride tests/test_ride.c)
    target_link_libraries(test_ride PRIVATE hta_engine m)
    add_test(NAME ride COMMAND test_ride)

    add_executable(test_bot tests/test_bot.c)
    target_link_libraries(test_bot PRIVATE hta_engine m)
    add_test(NAME bot COMMAND test_bot)

    add_executable(test_particle tests/test_particle.c)
    target_link_libraries(test_particle PRIVATE hta_engine m)
    add_test(NAME particle COMMAND test_particle)

    add_executable(test_projectile tests/test_projectile.c)
    target_link_libraries(test_projectile PRIVATE hta_engine m)
    add_test(NAME projectile COMMAND test_projectile)

    add_executable(test_weapons tests/test_weapons.c)
    target_link_libraries(test_weapons PRIVATE hta_engine m)
    add_test(NAME weapons COMMAND test_weapons)

    add_executable(test_hud tests/test_hud.c)
    target_link_libraries(test_hud PRIVATE hta_engine m)
    add_test(NAME hud COMMAND test_hud)

    add_executable(test_ammo tests/test_ammo.c)
    target_link_libraries(test_ammo PRIVATE hta_engine m)
    add_test(NAME ammo COMMAND test_ammo)

    add_executable(test_audio tests/test_audio.c)
    target_link_libraries(test_audio PRIVATE hta_engine m)
    add_test(NAME audio COMMAND test_audio)

    add_executable(test_sound tests/test_sound.c)
    target_link_libraries(test_sound PRIVATE hta_engine m)
    add_test(NAME sound COMMAND test_sound)


    add_executable(test_rigid tests/test_rigid.c)
    target_link_libraries(test_rigid PRIVATE hta_engine m)
    add_test(NAME rigid COMMAND test_rigid)

    # The procedural sound bank as WAV files, to listen to.
    add_executable(megamod-sfxdump src/tools/sfxdump.c)
    target_link_libraries(megamod-sfxdump PRIVATE hta_engine m)
    add_executable(test_report tests/test_report.c src/app/report.c)
    target_include_directories(test_report PRIVATE src)
    target_link_libraries(test_report PRIVATE m)
    add_test(NAME report COMMAND test_report)
    add_executable(test_session tests/test_session.c)
    target_link_libraries(test_session PRIVATE hta_engine m)
    add_test(NAME session COMMAND test_session)
    add_executable(test_sfx tests/test_sfx.c)
    target_link_libraries(test_sfx PRIVATE hta_engine m)
    add_test(NAME sfx COMMAND test_sfx)
    add_executable(test_world_fx tests/test_world_fx.c)
    target_link_libraries(test_world_fx PRIVATE hta_engine m)
    add_test(NAME world_fx COMMAND test_world_fx)

    add_executable(test_world_director tests/test_world_director.c)
    target_link_libraries(test_world_director PRIVATE hta_engine m)
    add_test(NAME world_director COMMAND test_world_director)

    add_executable(test_gfx_settings tests/test_gfx_settings.c)
    target_link_libraries(test_gfx_settings PRIVATE hta_engine m)
    add_test(NAME gfx_settings COMMAND test_gfx_settings)

    add_executable(mkfixture tests/mkfixture.c tests/fixture.c)
    target_include_directories(mkfixture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(mkfixture PRIVATE m)

    # Tests call side-effecting functions inside assert(). A Release build
    # defines NDEBUG, which compiled those calls out and left the tests
    # passing on garbage (test_external_map freed an unbuilt grid). Keep
    # asserts live in every test, whatever the build type.
    get_property(hta_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(t IN LISTS hta_targets)
        if(t MATCHES "^test_")
            target_compile_options(${t} PRIVATE -UNDEBUG)
        endif()
    endforeach()
endif()
