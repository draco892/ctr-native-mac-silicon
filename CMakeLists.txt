cmake_minimum_required(VERSION 3.20)
project(CTR-Native C)
include(CheckCCompilerFlag)

set(CTR_NATIVE_VERSION "0.1.0-beta.7.1")

option(CTR_BUILD_GAME "Build the game (currently requires 32-bit asset layouts)" ON)
option(CTR_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan for native porting tests" OFF)
include(CTest)

# Build actual production memory modules independently of resident game layouts
# and SDL. This is the ARM64 porting entry point until asset decoding is ready.
if(BUILD_TESTING)
    add_executable(ctr_native_memory_tests
        tests/native_memory_tests.c
        game/MEMPACK.c
        platform/native_mempack_arena.c
        platform/native_gpu_links.c
    )
    set_target_properties(ctr_native_memory_tests PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(ctr_native_memory_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(ctr_native_memory_tests PRIVATE CTR_NATIVE CTR_INTERNAL)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ctr_native_memory_tests PRIVATE -Wall -Wextra -Werror)
        if(CTR_ENABLE_SANITIZERS)
            target_compile_options(ctr_native_memory_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(ctr_native_memory_tests PRIVATE -fsanitize=address,undefined)
        endif()
    elseif(CTR_ENABLE_SANITIZERS)
        message(FATAL_ERROR "Native memory sanitizers require a GCC or Clang frontend.")
    endif()
    add_test(NAME ctr_native_memory COMMAND ctr_native_memory_tests)

    add_executable(ctr_native_lng_tests
        tests/native_lng_tests.c
        platform/native_lng.c
        game/MEMPACK.c
        platform/native_mempack_arena.c
    )
    set_target_properties(ctr_native_lng_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(ctr_native_lng_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(ctr_native_lng_tests PRIVATE CTR_NATIVE CTR_INTERNAL)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ctr_native_lng_tests PRIVATE -Wall -Wextra -Werror)
        if(CTR_ENABLE_SANITIZERS)
            target_compile_options(ctr_native_lng_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(ctr_native_lng_tests PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME ctr_native_lng COMMAND ctr_native_lng_tests)

    add_executable(ctr_native_ptrmap_tests tests/native_ptrmap_tests.c platform/native_ptrmap.c)
    set_target_properties(ctr_native_ptrmap_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(ctr_native_ptrmap_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(ctr_native_ptrmap_tests PRIVATE CTR_NATIVE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ctr_native_ptrmap_tests PRIVATE -Wall -Wextra -Werror)
        if(CTR_ENABLE_SANITIZERS)
            target_compile_options(ctr_native_ptrmap_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(ctr_native_ptrmap_tests PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME ctr_native_ptrmap COMMAND ctr_native_ptrmap_tests)

    add_executable(ctr_native_assets_tests tests/native_assets_tests.c platform/native_asset_readers.c platform/native_ptrmap.c platform/native_asset_loading.c)
    set_target_properties(ctr_native_assets_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(ctr_native_assets_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(ctr_native_assets_tests PRIVATE CTR_NATIVE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ctr_native_assets_tests PRIVATE -Wall -Wextra -Werror)
        if(CTR_ENABLE_SANITIZERS)
            target_compile_options(ctr_native_assets_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(ctr_native_assets_tests PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME ctr_native_assets COMMAND ctr_native_assets_tests)

    add_executable(ctr_native_model_library_tests tests/native_model_library_tests.c platform/native_model_library.c platform/native_asset_readers.c platform/native_ptrmap.c)
    set_target_properties(ctr_native_model_library_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(ctr_native_model_library_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(ctr_native_model_library_tests PRIVATE CTR_NATIVE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ctr_native_model_library_tests PRIVATE -Wall -Wextra -Werror)
        if(CTR_ENABLE_SANITIZERS)
            target_compile_options(ctr_native_model_library_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(ctr_native_model_library_tests PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME ctr_native_model_library COMMAND ctr_native_model_library_tests)

    add_executable(ctr_native_model_animation_tests tests/native_model_animation_tests.c platform/native_model_animation.c platform/native_asset_readers.c platform/native_ptrmap.c)
    set_target_properties(ctr_native_model_animation_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(ctr_native_model_animation_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(ctr_native_model_animation_tests PRIVATE CTR_NATIVE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ctr_native_model_animation_tests PRIVATE -Wall -Wextra -Werror)
        if(CTR_ENABLE_SANITIZERS)
            target_compile_options(ctr_native_model_animation_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(ctr_native_model_animation_tests PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME ctr_native_model_animation COMMAND ctr_native_model_animation_tests)

    add_executable(ctr_native_model_vertices_tests tests/native_model_vertices_tests.c platform/native_model_vertices.c platform/native_model_animation.c platform/native_asset_readers.c platform/native_ptrmap.c)
    set_target_properties(ctr_native_model_vertices_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(ctr_native_model_vertices_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(ctr_native_model_vertices_tests PRIVATE CTR_NATIVE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ctr_native_model_vertices_tests PRIVATE -Wall -Wextra -Werror)
        if(CTR_ENABLE_SANITIZERS)
            target_compile_options(ctr_native_model_vertices_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(ctr_native_model_vertices_tests PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME ctr_native_model_vertices COMMAND ctr_native_model_vertices_tests)

    add_executable(ctr_native_model_transform_tests tests/native_model_transform_tests.c platform/native_model_transform.c platform/native_model_vertices.c platform/native_model_animation.c platform/native_asset_readers.c platform/native_ptrmap.c)
    set_target_properties(ctr_native_model_transform_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(ctr_native_model_transform_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(ctr_native_model_transform_tests PRIVATE CTR_NATIVE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ctr_native_model_transform_tests PRIVATE -Wall -Wextra -Werror)
        if(CTR_ENABLE_SANITIZERS)
            target_compile_options(ctr_native_model_transform_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(ctr_native_model_transform_tests PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME ctr_native_model_transform COMMAND ctr_native_model_transform_tests)

    add_executable(ctr_native_model_commands_tests tests/native_model_commands_tests.c platform/native_model_commands.c platform/native_asset_readers.c platform/native_ptrmap.c)
    set_target_properties(ctr_native_model_commands_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(ctr_native_model_commands_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(ctr_native_model_commands_tests PRIVATE CTR_NATIVE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ctr_native_model_commands_tests PRIVATE -Wall -Wextra -Werror)
        if(CTR_ENABLE_SANITIZERS)
            target_compile_options(ctr_native_model_commands_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(ctr_native_model_commands_tests PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME ctr_native_model_commands COMMAND ctr_native_model_commands_tests)

    add_executable(ctr_native_asset_validate tools/native_asset_validate.c platform/native_asset_loading.c platform/native_asset_readers.c platform/native_model_library.c platform/native_model_animation.c platform/native_model_vertices.c platform/native_model_commands.c platform/native_model_transform.c platform/native_ptrmap.c platform/native_disc_image.c)
    set_target_properties(ctr_native_asset_validate PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(ctr_native_asset_validate PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(ctr_native_asset_validate PRIVATE CTR_NATIVE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ctr_native_asset_validate PRIVATE -Wall -Wextra -Werror)
        if(CTR_ENABLE_SANITIZERS)
            target_compile_options(ctr_native_asset_validate PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(ctr_native_asset_validate PRIVATE -fsanitize=address,undefined)
        endif()
    endif()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME ctr_native_asset_validate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_asset_validate_tests.py
            $<TARGET_FILE:ctr_native_asset_validate>)
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/ctr-u.bin")
        add_test(NAME ctr_native_asset_retail
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_asset_retail_tests.py
                $<TARGET_FILE:ctr_native_asset_validate> ${CMAKE_SOURCE_DIR}/assets)
        set_tests_properties(ctr_native_asset_retail PROPERTIES LABELS retail)
    endif()
    add_test(
        NAME ctr_match_unit
        COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_SOURCE_DIR}/tools/matching
            -p test_*.py
    )
    add_test(NAME ctr_native_memory_rejections
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native_memory_rejections.py
            $<TARGET_FILE:ctr_native_memory_tests>
    )
endif()

if(NOT CTR_BUILD_GAME)
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR "CTR_BUILD_GAME=OFF requires BUILD_TESTING=ON.")
    endif()
    return()
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "CTR Native currently requires a 32-bit target. "
        "Select Win32 for MSVC, an i686 MinGW compiler on Windows, or pass -m32 "
        "in the initial compiler and linker flags on Linux. "
        "For ARM64 porting tests, use the macos-arm64-memory preset."
    )
endif()

set(CTR_NATIVE_MSVC_FRONTEND OFF)
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    set(CTR_NATIVE_MSVC_FRONTEND ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE CTR_NATIVE_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CTR_NATIVE_GIT_HASH)
    set(CTR_NATIVE_GIT_HASH "unknown")
endif()

execute_process(
    COMMAND git diff --quiet --exit-code
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE CTR_NATIVE_GIT_DIRTY
    ERROR_QUIET
)
if(CTR_NATIVE_GIT_DIRTY EQUAL 0)
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}")
else()
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}-dirty")
endif()

function(ctr_native_add_label_warning_if_supported target warning flag_check)
    set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror=unknown-warning-option")
    endif()

    check_c_compiler_flag("${warning}" "${flag_check}")
    set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")

    if(${flag_check})
        target_compile_options(${target} PRIVATE "${warning}")
    endif()
endfunction()

# SDL3
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(externals/SDL)

# CTR Native
add_executable(ctr_native main.c)
set_target_properties(ctr_native PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_include_directories(ctr_native PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(ctr_native PRIVATE
    CTR_NATIVE
    CTR_INTERNAL
    CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\"
    CTR_NATIVE_BUILD_ID=\"${CTR_NATIVE_BUILD_ID}\"
)
if(CTR_NATIVE_MSVC_FRONTEND)
    target_compile_options(ctr_native PRIVATE
        /W4 #TODO(aalhendi): fix warnings
        /bigobj
        /arch:SSE2
        /wd4152
        /wd4244
        /wd4245
        /wd4310
        /wd5287
    )
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86|x86_64|AMD64)$")
        target_compile_options(ctr_native PRIVATE -msse)
    endif()
    target_compile_options(ctr_native PRIVATE
        -Wenum-conversion
        -Wenum-compare
        -Wswitch-enum
        -Wformat
        -Wparentheses
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Release>:-g>
    )

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(ctr_native PRIVATE -Wall -Wextra -Wstrict-aliasing=2)
        ctr_native_add_label_warning_if_supported(ctr_native -Wfree-labels CTR_HAS_WFREE_LABELS)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        ctr_native_add_label_warning_if_supported(ctr_native -Wc23-extensions CTR_HAS_WC23_EXTENSIONS)
    endif()
endif()
target_link_libraries(ctr_native SDL3::SDL3)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_options(ctr_native PRIVATE -static-libgcc)
endif()
if(MINGW)
    target_link_options(ctr_native PRIVATE -static)
endif()

if(BUILD_TESTING)
    add_test(NAME ctr_native_version COMMAND $<TARGET_FILE:ctr_native> --version)
    set_tests_properties(ctr_native_version PROPERTIES PASS_REGULAR_EXPRESSION "^CTR Native ")

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_executable(ctr_particle_renderer_smoke
            tests/particle_renderer_smoke.c
            platform/native_gpu_links.c
        )
        set_target_properties(ctr_particle_renderer_smoke PROPERTIES
            C_STANDARD 17
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
        target_include_directories(ctr_particle_renderer_smoke PRIVATE ${CMAKE_SOURCE_DIR}/include)
        target_compile_definitions(ctr_particle_renderer_smoke PRIVATE CTR_NATIVE)
        target_compile_options(ctr_particle_renderer_smoke PRIVATE
            -O2 -Wall -Wextra -ffunction-sections -fdata-sections
        )
        target_link_options(ctr_particle_renderer_smoke PRIVATE -Wl,--gc-sections)
        add_test(NAME ctr_particle_renderer_smoke COMMAND $<TARGET_FILE:ctr_particle_renderer_smoke>)
    endif()
endif()
