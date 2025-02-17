#!/bin/bash

set -e

SDL_SHA=b5c3eab6b447111d3c7879bb547b80fb4abd9063
SDL_IMAGE_SHA=4a762bdfb7b43dae7a8a818567847881e49bdab4
SDL_TTF_SHA=07e4d1241817f2c0f81749183fac5ec82d7bbd72
SDL_MIXER_SHA=4be37aed1a4b76df71a814fbfa8ec9983f3b5508
FREEIMAGE_SHA=ef590ee9ccc2b3042a666d9f812f67cdfb2fc7ca
BGFX_CMAKE_VERSION=1.129.8863-490
BGFX_PATCH_SHA=1d0967155c375155d1f778ded4061f35c80fc96f
PINMAME_SHA=be86b9665cf9bda306d0a7ae9d6c7fdfc4679b71
OPENXR_SHA=b15ef6ce120dad1c7d3ff57039e73ba1a9f17102
LIBDMDUTIL_SHA=04cde19a5fe4e075b12d221c6f073641a39a77ca

echo "Building external libraries..."
echo "  SDL_SHA: ${SDL_SHA}"
echo "  SDL_IMAGE_SHA: ${SDL_IMAGE_SHA}"
echo "  SDL_TTF_SHA: ${SDL_TTF_SHA}"
echo "  SDL_MIXER_SHA: ${SDL_MIXER_SHA}"
echo "  FREEIMAGE_SHA: ${FREEIMAGE_SHA}"
echo "  BGFX_CMAKE_VERSION: ${BGFX_CMAKE_VERSION}"
echo "  BGFX_PATCH_SHA: ${BGFX_PATCH_SHA}"
echo "  PINMAME_SHA: ${PINMAME_SHA}"
echo "  OPENXR_SHA: ${OPENXR_SHA}"
echo "  LIBDMDUTIL_SHA: ${LIBDMDUTIL_SHA}"
echo ""

if [ -z "${BUILD_TYPE}" ]; then
   BUILD_TYPE="Release"
fi

echo "Build type: ${BUILD_TYPE}"
echo ""

mkdir -p "cache/${BUILD_TYPE}"
cd "cache/${BUILD_TYPE}"

#
# build SDL3, SDL3_image, SDL3_ttf, SDL3_mixer
#

SDL3_EXPECTED_SHA="${SDL_SHA}-${SDL_IMAGE_SHA}-${SDL_TTF_SHA}-${SDL_MIXER_SHA}"

if [ ! -f "SDL3/cache.txt" ] || [ "$(cat "SDL3/cache.txt")" != "${SDL3_EXPECTED_SHA}" ]; then
   rm -rf SDL3
   mkdir SDL3
   cd SDL3

   curl -sL https://github.com/libsdl-org/SDL/archive/${SDL_SHA}.tar.gz -o SDL-${SDL_SHA}.tar.gz
   tar xzf SDL-${SDL_SHA}.tar.gz
   mv SDL-${SDL_SHA} SDL
   cd SDL
   cmake \
      -G "Visual Studio 17 2022" \
      -A Win32 \
      -DSDL_SHARED=ON \
      -DSDL_STATIC=OFF \
      -DSDL_TEST_LIBRARY=OFF \
      -B build
   cmake --build build --config ${BUILD_TYPE}
   cd ..

   curl -sL https://github.com/libsdl-org/SDL_image/archive/${SDL_IMAGE_SHA}.tar.gz -o SDL_image-${SDL_IMAGE_SHA}.tar.gz
   tar xzf SDL_image-${SDL_IMAGE_SHA}.tar.gz --exclude='*/Xcode/*'
   mv SDL_image-${SDL_IMAGE_SHA} SDL_image
   cd SDL_image
   ./external/download.sh
   cmake \
      -G "Visual Studio 17 2022" \
      -A Win32 \
      -DBUILD_SHARED_LIBS=ON \
      -DSDLIMAGE_SAMPLES=OFF \
      -DSDLIMAGE_DEPS_SHARED=ON \
      -DSDLIMAGE_VENDORED=ON \
      -DSDLIMAGE_AVIF=OFF \
      -DSDLIMAGE_WEBP=OFF \
      -DSDL3_DIR=../SDL/build \
      -B build
   cmake --build build --config ${BUILD_TYPE}
   cd ..

   curl -sL https://github.com/libsdl-org/SDL_ttf/archive/${SDL_TTF_SHA}.tar.gz -o SDL_ttf-${SDL_TTF_SHA}.tar.gz
   tar xzf SDL_ttf-${SDL_TTF_SHA}.tar.gz --exclude='*/Xcode/*'
   mv SDL_ttf-${SDL_TTF_SHA} SDL_ttf
   cd SDL_ttf
   ./external/download.sh
   cmake \
      -G "Visual Studio 17 2022" \
      -A Win32 \
      -DBUILD_SHARED_LIBS=ON \
      -DSDLTTF_SAMPLES=OFF \
      -DSDLTTF_VENDORED=ON \
      -DSDLTTF_HARFBUZZ=ON \
      -DSDL3_DIR=../SDL/build \
      -B build
   cmake --build build --config ${BUILD_TYPE}
   cd ..

   curl -sL https://github.com/libsdl-org/SDL_mixer/archive/${SDL_MIXER_SHA}.tar.gz -o SDL_mixer-${SDL_MIXER_SHA}.tar.gz
   tar xzf SDL_mixer-${SDL_MIXER_SHA}.tar.gz --exclude='*/Xcode/*'
   mv SDL_mixer-${SDL_MIXER_SHA} SDL_mixer
   cd SDL_mixer
   ./external/download.sh
   cmake \
      -G "Visual Studio 17 2022" \
      -A Win32 \
      -DBUILD_SHARED_LIBS=ON \
      -DSDLMIXER_SAMPLES=OFF \
      -DSDLMIXER_VENDORED=ON \
      -DSDL3_DIR=../SDL/build \
      -B build
   cmake --build build --config ${BUILD_TYPE}
   cd ..

   echo "$SDL3_EXPECTED_SHA" > cache.txt

   cd ..
fi

#
# build freeimage
#

FREEIMAGE_EXPECTED_SHA="${FREEIMAGE_SHA}"

if [ ! -f "freeimage/cache.txt" ] || [ "$(cat "freeimage/cache.txt")" != "${FREEIMAGE_EXPECTED_SHA}" ]; then
   rm -rf freeimage
   mkdir freeimage
   cd freeimage

   curl -sL https://github.com/toxieainc/freeimage/archive/${FREEIMAGE_SHA}.tar.gz -o freeimage-${FREEIMAGE_SHA}.tar.gz
   tar xzf freeimage-${FREEIMAGE_SHA}.tar.gz
   mv freeimage-${FREEIMAGE_SHA} freeimage
   cd freeimage
   cmake \
      -G "Visual Studio 17 2022" \
      -A Win32 \
      -DPLATFORM=win \
      -DARCH=x86 \
      -DBUILD_SHARED=ON \
      -DBUILD_STATIC=OFF \
      -B build
   cmake --build build --config ${BUILD_TYPE}
   cd ..

   echo "$FREEIMAGE_EXPECTED_SHA" > cache.txt

   cd ..
fi

#
# build bgfx
#

BGFX_EXPECTED_SHA="${BGFX_CMAKE_VERSION}-${BGFX_PATCH_SHA}"

if [ ! -f "bgfx/cache.txt" ] || [ "$(cat "bgfx/cache.txt")" != "${BGFX_EXPECTED_SHA}" ]; then
   rm -rf bgfx
   mkdir bgfx
   cd bgfx

   curl -sL https://github.com/bkaradzic/bgfx.cmake/releases/download/v${BGFX_CMAKE_VERSION}/bgfx.cmake.v${BGFX_CMAKE_VERSION}.tar.gz -o bgfx.cmake.v${BGFX_CMAKE_VERSION}.tar.gz
   tar xzf bgfx.cmake.v${BGFX_CMAKE_VERSION}.tar.gz
   curl -sL https://github.com/vbousquet/bgfx/archive/${BGFX_PATCH_SHA}.tar.gz -o bgfx-${BGFX_PATCH_SHA}.tar.gz
   tar xzf bgfx-${BGFX_PATCH_SHA}.tar.gz
   cd bgfx.cmake
   rm -rf bgfx
   mv ../bgfx-${BGFX_PATCH_SHA} bgfx
   cmake -G "Visual Studio 17 2022" \
      -S. \
      -A Win32 \
      -DBGFX_LIBRARY_TYPE=SHARED \
      -DBGFX_BUILD_TOOLS=OFF \
      -DBGFX_BUILD_EXAMPLES=OFF \
      -DBGFX_CONFIG_MULTITHREADED=ON \
      -DBGFX_CONFIG_MAX_FRAME_BUFFERS=256 \
      -B build
   cmake --build build --config ${BUILD_TYPE}
   cd ..

   echo "$BGFX_EXPECTED_SHA" > cache.txt

   cd ..
fi

#
# build pinmame
#

PINMAME_EXPECTED_SHA="${PINMAME_SHA}"

if [ ! -f "pinmame/cache.txt" ] || [ "$(cat "pinmame/cache.txt")" != "${PINMAME_EXPECTED_SHA}" ]; then
   rm -rf pinmame
   mkdir pinmame
   cd pinmame

   curl -sL https://github.com/vbousquet/pinmame/archive/${PINMAME_SHA}.zip -o pinmame-${PINMAME_SHA}.zip
   unzip pinmame-${PINMAME_SHA}.zip
   mv pinmame-${PINMAME_SHA} pinmame
   cd pinmame
   cp cmake/libpinmame/CMakelists.txt .
   cmake \
      -G "Visual Studio 17 2022" \
      -A Win32 \
      -DPLATFORM=win \
      -DARCH=x86 \
      -DBUILD_SHARED=ON \
      -DBUILD_STATIC=OFF \
      -B build
   cmake --build build --config ${BUILD_TYPE}
   cd ..

   echo "$PINMAME_EXPECTED_SHA" > cache.txt

   cd ..
fi

#
# build openxr
#

OPENXR_EXPECTED_SHA="${OPENXR_SHA}"

if [ ! -f "openxr/cache.txt" ] || [ "$(cat "openxr/cache.txt")" != "${OPENXR_EXPECTED_SHA}" ]; then
   rm -rf openxr
   mkdir openxr
   cd openxr

   curl -sL https://github.com/KhronosGroup/OpenXR-SDK-Source/archive/${OPENXR_SHA}.tar.gz -o OpenXR-SDK-Source-${OPENXR_SHA}.zip
   tar xzf OpenXR-SDK-Source-${OPENXR_SHA}.zip
   mv OpenXR-SDK-Source-${OPENXR_SHA} openxr
   cd openxr
   cmake \
      -G "Visual Studio 17 2022" \
      -A Win32 \
      -DBUILD_TESTS=OFF \
      -DDYNAMIC_LOADER=ON \
      -B build
   cmake --build build --config ${BUILD_TYPE}
   cd ..

   echo "$OPENXR_EXPECTED_SHA" > cache.txt

   cd ..
fi

#
# build libdmdutil
#

LIBDMDUTIL_EXPECTED_SHA="${LIBDMDUTIL_SHA}"

if [ ! -f "libdmdutil/cache.txt" ] || [ "$(cat "libdmdutil/cache.txt")" != "${LIBDMDUTIL_EXPECTED_SHA}" ]; then
   rm -rf libdmdutil
   mkdir libdmdutil
   cd libdmdutil

   curl -sL https://github.com/vpinball/libdmdutil/archive/${LIBDMDUTIL_SHA}.tar.gz -o libdmdutil-${LIBDMDUTIL_SHA}.tar.gz
   tar xzf libdmdutil-${LIBDMDUTIL_SHA}.tar.gz
   mv libdmdutil-${LIBDMDUTIL_SHA} libdmdutil
   cd libdmdutil
   ./platforms/win/x86/external.sh
   cmake \
      -G "Visual Studio 17 2022" \
      -A Win32 \
      -DPLATFORM=win \
      -DARCH=x86 \
      -DBUILD_SHARED=ON \
      -DBUILD_STATIC=OFF \
      -B build
   cmake --build build --config ${BUILD_TYPE}
   cd ..

   echo "$LIBDMDUTIL_EXPECTED_SHA" > cache.txt

   cd ..
fi

#
# copy libraries
#

cp SDL3/SDL/build/${BUILD_TYPE}/SDL3.lib ../../../../../third-party/build-libs/windows-x86
cp SDL3/SDL/build/${BUILD_TYPE}/SDL3.dll ../../../../../third-party/runtime-libs/windows-x86
cp -r SDL3/SDL/include/SDL3 ../../../../../third-party/include/

cp SDL3/SDL_image/build/${BUILD_TYPE}/SDL3_image.lib ../../../../../third-party/build-libs/windows-x86
cp SDL3/SDL_image/build/${BUILD_TYPE}/SDL3_image.dll ../../../../../third-party/runtime-libs/windows-x86
cp -r SDL3/SDL_image/include/SDL3_image ../../../../../third-party/include/

cp SDL3/SDL_ttf/build/${BUILD_TYPE}/SDL3_ttf.lib ../../../../../third-party/build-libs/windows-x86
cp SDL3/SDL_ttf/build/${BUILD_TYPE}/SDL3_ttf.dll ../../../../../third-party/runtime-libs/windows-x86
cp -r SDL3/SDL_ttf/include/SDL3_ttf ../../../../../third-party/include/

cp SDL3/SDL_mixer/build/${BUILD_TYPE}/SDL3_mixer.lib ../../../../../third-party/build-libs/windows-x86
cp SDL3/SDL_mixer/build/${BUILD_TYPE}/SDL3_mixer.dll ../../../../../third-party/runtime-libs/windows-x86
cp -r SDL3/SDL_mixer/include/SDL3_mixer ../../../../../third-party/include/

cp freeimage/freeimage/build/${BUILD_TYPE}/freeimage.lib ../../../../../third-party/build-libs/windows-x86
cp freeimage/freeimage/build/${BUILD_TYPE}/freeimage.dll ../../../../../third-party/runtime-libs/windows-x86
cp freeimage/freeimage/Source/FreeImage.h ../../../../../third-party/include

cp bgfx/bgfx.cmake/build/cmake/bgfx/${BUILD_TYPE}/bgfx.lib ../../../../../third-party/build-libs/windows-x86
cp bgfx/bgfx.cmake/build/cmake/bgfx/${BUILD_TYPE}/bgfx.dll ../../../../../third-party/runtime-libs/windows-x86
cp -r bgfx/bgfx.cmake/bgfx/include/bgfx ../../../../../third-party/include/
cp bgfx/bgfx.cmake/build/cmake/bimg/${BUILD_TYPE}/bimg.lib ../../../../../third-party/build-libs/windows-x86
cp bgfx/bgfx.cmake/build/cmake/bimg/${BUILD_TYPE}/bimg_decode.lib ../../../../../third-party/build-libs/windows-x86
cp bgfx/bgfx.cmake/build/cmake/bimg/${BUILD_TYPE}/bimg_encode.lib ../../../../../third-party/build-libs/windows-x86
cp -r bgfx/bgfx.cmake/bimg/include/bimg ../../../../../third-party/include/
cp bgfx/bgfx.cmake/build/cmake/bx/${BUILD_TYPE}/bx.lib ../../../../../third-party/build-libs/windows-x86
cp -r bgfx/bgfx.cmake/bx/include/bx ../../../../../third-party/include/

cp pinmame/pinmame/build/${BUILD_TYPE}/pinmame.lib ../../../../../third-party/build-libs/windows-x86
cp pinmame/pinmame/build/${BUILD_TYPE}/pinmame.dll ../../../../../third-party/runtime-libs/windows-x86
cp pinmame/pinmame/src/libpinmame/libpinmame.h ../../../../../third-party/include
cp pinmame/pinmame/src/libpinmame/pinmamedef.h ../../../../../third-party/include

cp openxr/openxr/build/src/loader/${BUILD_TYPE}/openxr_loader.lib ../../../../../third-party/build-libs/windows-x86
cp openxr/openxr/build/src/loader/${BUILD_TYPE}/openxr_loader.dll ../../../../../third-party/runtime-libs/windows-x86
cp -r openxr/openxr/include/openxr ../../../../../third-party/include

cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/dmdutil.lib ../../../../../third-party/build-libs/windows-x86
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/dmdutil.dll ../../../../../third-party/runtime-libs/windows-x86
cp -r libdmdutil/libdmdutil/include/DMDUtil ../../../../../third-party/include/
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/zedmd.lib ../../../../../third-party/build-libs/windows-x86
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/zedmd.dll ../../../../../third-party/runtime-libs/windows-x86
cp libdmdutil/libdmdutil/third-party/include/ZeDMD.h ../../../../../third-party/include
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/serum.lib ../../../../../third-party/build-libs/windows-x86
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/serum.dll ../../../../../third-party/runtime-libs/windows-x86
cp libdmdutil/libdmdutil/third-party/include/serum.h ../../../../../third-party/include
cp libdmdutil/libdmdutil/third-party/include/serum-decode.h ../../../../../third-party/include
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/libserialport.lib ../../../../../third-party/build-libs/windows-x86
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/libserialport.dll ../../../../../third-party/runtime-libs/windows-x86
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/pupdmd.lib ../../../../../third-party/build-libs/windows-x86
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/pupdmd.dll ../../../../../third-party/runtime-libs/windows-x86
cp libdmdutil/libdmdutil/third-party/include/pupdmd.h ../../../../../third-party/include
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/sockpp.lib ../../../../../third-party/build-libs/windows-x86
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/sockpp.dll ../../../../../third-party/runtime-libs/windows-x86
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/cargs.lib ../../../../../third-party/build-libs/windows-x86
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/cargs.dll ../../../../../third-party/runtime-libs/windows-x86