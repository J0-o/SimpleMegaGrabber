cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED VCPKG_ROOT)
    set(VCPKG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vcpkg" CACHE PATH "vcpkg checkout")
endif()

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")
endif()

set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mega-sdk" CACHE PATH "")
set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mega-sdk/cmake/vcpkg_overlay_ports" CACHE STRING "")
set(VCPKG_OVERLAY_TRIPLETS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/triplets;${CMAKE_CURRENT_SOURCE_DIR}/vendor/mega-sdk/cmake/vcpkg_overlay_triplets"
    CACHE STRING "" FORCE)
set(VCPKG_TARGET_TRIPLET "x64-windows-mega" CACHE STRING "" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(SimpleMegaGrabber VERSION 1.0.0 LANGUAGES C CXX)

set(ENABLE_SYNC OFF CACHE BOOL "" FORCE)
set(ENABLE_CHAT OFF CACHE BOOL "" FORCE)
set(ENABLE_JAVA_BINDINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_QT_BINDINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_MEDIA_FILE_METADATA OFF CACHE BOOL "" FORCE)
set(ENABLE_DRIVE_NOTIFICATIONS OFF CACHE BOOL "" FORCE)
set(ENABLE_ISOLATED_GFX OFF CACHE BOOL "" FORCE)
set(ENABLE_SDKLIB_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENABLE_SDKLIB_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SDKLIB_WERROR OFF CACHE BOOL "" FORCE)
set(MEGA_USE_WSUPLOAD OFF CACHE BOOL "" FORCE)
set(USE_FREEIMAGE OFF CACHE BOOL "" FORCE)
set(USE_FFMPEG OFF CACHE BOOL "" FORCE)
set(USE_LIBUV OFF CACHE BOOL "" FORCE)
set(USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(USE_PDFIUM OFF CACHE BOOL "" FORCE)
set(USE_READLINE OFF CACHE BOOL "" FORCE)

add_subdirectory(vendor/mega-sdk EXCLUDE_FROM_ALL)

add_executable(SimpleMegaGrabber src/main.cpp)
target_compile_features(SimpleMegaGrabber PRIVATE cxx_std_17)
target_compile_definitions(SimpleMegaGrabber PRIVATE UNICODE _UNICODE)
target_link_libraries(SimpleMegaGrabber PRIVATE MEGA::SDKlib)

if(MSVC)
    target_compile_options(SimpleMegaGrabber PRIVATE /W4 /permissive- /utf-8)
endif()
