cmake_minimum_required(VERSION 3.22.1)

project(Wemu)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wno-unused-function -Wimplicit-function-declaration")

add_subdirectory(adrenotools)

#
# There was an `xr/` here, and a vendored OpenXR SDK beside it, and neither
# had anything to call into them.
#
# The eight JNI entry points it defined bound to a VR activity in a pre-fork
# package, and no such class exists anywhere in this repo. AppUtils still
# carries the note saying it "was
# the last thing in the engine that referred to the VR activity", so the Java
# half was removed at some point and the native half was left behind: 2,142
# lines of glue compiled into libwemu, 415 files of vendored SDK, and a
# 12MB libopenxr_loader.so packaged into every APK for nothing.
#
# EGL and the GLES libraries went with it. Nothing else in this target
# references a GL symbol; the remaining sources are Vulkan, ALSA and the
# X connector.
#
add_library(wemu SHARED
        wemu/process_helper.c
        wemu/drawable.c
        wemu/gpu_image.c
        wemu/sysvshared_memory.c
        wemu/xconnector_epoll.c
        wemu/alsa_client.c
        wemu/vulkan.c
)

target_compile_options(wemu PRIVATE -Wall -Wextra)

target_link_libraries(wemu
        log
        android
        jnigraphics
        aaudio
        dl
        adrenotools
)

add_library(fakeinput SHARED
        wemu/fakeinput.cpp
)

target_compile_options(fakeinput PRIVATE -Wall -Wextra -fvisibility=hidden)

target_link_libraries(fakeinput
        log
        dl
)

# --- Shader compilation ------------------------------------------------------
#
# Upstream calls `glslangValidator` and assumes it is on PATH, which it is only
# if the Vulkan SDK happens to be installed. The Android NDK ships `glslc`
# instead, so a clean checkout on a machine with nothing but Android Studio
# failed here with "glslangValidator is not recognized" after building all 61
# other targets.
#
# Either will do. glslangValidator is preferred where present so the generated
# headers stay byte-identical to upstream's; otherwise glslc produces the same
# SPIR-V and a tiny script wraps it in the same declaration.

find_program(GLSLANG_VALIDATOR glslangValidator)
find_program(GLSLC glslc
    HINTS
        "$ENV{ANDROID_NDK_HOME}/shader-tools/windows-x86_64"
        "$ENV{ANDROID_NDK_HOME}/shader-tools/linux-x86_64"
        "$ENV{ANDROID_NDK_HOME}/shader-tools/darwin-x86_64"
        "${ANDROID_NDK}/shader-tools/windows-x86_64"
        "${ANDROID_NDK}/shader-tools/linux-x86_64"
        "${ANDROID_NDK}/shader-tools/darwin-x86_64"
)

if(NOT GLSLANG_VALIDATOR AND NOT GLSLC)
    message(FATAL_ERROR
        "No shader compiler found. Install the Vulkan SDK for glslangValidator, "
        "or ensure the NDK's shader-tools directory is present for glslc.")
endif()

# compile_shader(<source> <variable name> <output header>)
function(compile_shader SOURCE VARNAME OUTPUT)
    if(GLSLANG_VALIDATOR)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT}
            COMMAND "${GLSLANG_VALIDATOR}" -V --vn ${VARNAME}
                    -x -o ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT}
                    ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}
            COMMENT "Compiling ${SOURCE} -> ${OUTPUT} (glslangValidator)"
        )
    else()
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT}
            COMMAND ${CMAKE_COMMAND}
                    -DGLSLC=${GLSLC}
                    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}
                    -DVARNAME=${VARNAME}
                    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/WrapSpirv.cmake
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/WrapSpirv.cmake
            COMMENT "Compiling ${SOURCE} -> ${OUTPUT} (glslc)"
        )
    endif()
endfunction()

compile_shader(wemu/window.vert window_vert_code window_vert.h)

compile_shader(wemu/window.frag window_frag_code window_frag.h)

compile_shader(wemu/window_sgsr.frag window_sgsr_frag_code window_sgsr_frag.h)

compile_shader(wemu/window_stretch.frag window_stretch_frag_code window_stretch_frag.h)

compile_shader(wemu/window_fsr.frag window_fsr_frag_code window_fsr_frag.h)

compile_shader(wemu/window_finish.frag window_finish_frag_code window_finish_frag.h)

compile_shader(wemu/window_nis.frag window_nis_frag_code window_nis_frag.h)

compile_shader(wemu/window_postfx.frag window_postfx_frag_code window_postfx_frag.h)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_library(vulkan_renderer SHARED
        wemu/vulkan_jni.cpp
        wemu/VulkanRendererContext.cpp

        ${CMAKE_CURRENT_BINARY_DIR}/window_vert.h
        ${CMAKE_CURRENT_BINARY_DIR}/window_frag.h
        ${CMAKE_CURRENT_BINARY_DIR}/window_sgsr_frag.h
        ${CMAKE_CURRENT_BINARY_DIR}/window_stretch_frag.h
        ${CMAKE_CURRENT_BINARY_DIR}/window_fsr_frag.h
        ${CMAKE_CURRENT_BINARY_DIR}/window_finish_frag.h
        ${CMAKE_CURRENT_BINARY_DIR}/window_nis_frag.h
        ${CMAKE_CURRENT_BINARY_DIR}/window_postfx_frag.h
)

target_compile_options(vulkan_renderer PRIVATE
        -Wall -Wextra -fvisibility=hidden -std=c++17
)

target_link_libraries(vulkan_renderer
        log
        android
        vulkan
        adrenotools
        dl
        atomic
)

# EGL/OpenGL ES compositor. It implements the same Java-side scene contract as
# vulkan_renderer, including CPU and AHardwareBuffer windows, but uses the
# platform GLES stack for final composition. Keeping it in a separate library
# means selecting OpenGL never initializes Vulkan (or a custom Vulkan ICD).
add_library(opengl_renderer SHARED
        wemu/opengl_jni.cpp
        wemu/OpenGLRendererContext.cpp
)

target_compile_options(opengl_renderer PRIVATE
        -O3 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden -std=c++17
)

target_link_libraries(opengl_renderer
        log
        android
        EGL
        GLESv3
        dl
        atomic
)

# --- The scanout renderer ----------------------------------------------------
#
# An alternative to `vulkan_renderer` that does not composite at all: it hands
# the guest window's AHardwareBuffer straight to SurfaceFlinger through
# ASurfaceControl, so there is no texture upload, no draw and no swap.
#
# Every ASurfaceControl entry point is resolved with `dlsym` against
# libandroid.so at runtime rather than linked, which is what makes this
# buildable here at all: those symbols are guarded by `__ANDROID_API__ >= 29`
# in the NDK headers and this project compiles against minSdk 26. The library
# checks the running API level and reports itself unavailable below 29, so the
# renderer can be offered and then decline rather than fail to load.

add_library(asurface_renderer SHARED
        asurfacerenderer/asurface_jni.cpp
        asurfacerenderer/ASurfaceRendererContext.cpp
        asurfacerenderer/blit_converter.cpp
        asurfacerenderer/second_screen.cpp
)

target_compile_options(asurface_renderer PRIVATE
        -O3 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden -std=c++17
)

target_link_libraries(asurface_renderer
        log
        android
        dl
        atomic
        EGL
        GLESv3
)

# The AHardwareBuffer swapchain behind `AHBImage`.
#
# Separate from `gpu_image.c` in libwemu, which backs `GPUImage`, rather
# than shared: the two export JNI symbols named after their own Java classes,
# and this one carries a three-slot swapchain and its fences that the Vulkan
# path has no use for.

add_library(ahbimage SHARED
        asurfacerenderer/ahbimage.c
)

target_compile_options(ahbimage PRIVATE
        -O3 -Wall -Wextra -Wno-unused-parameter
)

target_link_libraries(ahbimage
        log
        android
        jnigraphics
        m
)
