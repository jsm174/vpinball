#!/bin/bash

set -e

SDL_SHA=b5c3eab6b447111d3c7879bb547b80fb4abd9063
SDL_IMAGE_SHA=4a762bdfb7b43dae7a8a818567847881e49bdab4
SDL_TTF_SHA=fc618f0e884f2b4476bbf760067e5427ef6ed044
SDL_MIXER_SHA=4be37aed1a4b76df71a814fbfa8ec9983f3b5508
FREEIMAGE_SHA=ef590ee9ccc2b3042a666d9f812f67cdfb2fc7ca
BGFX_CMAKE_VERSION=1.129.8863-490
BGFX_PATCH_SHA=1d0967155c375155d1f778ded4061f35c80fc96f
PINMAME_SHA=be86b9665cf9bda306d0a7ae9d6c7fdfc4679b71
OPENXR_SHA=b15ef6ce120dad1c7d3ff57039e73ba1a9f17102
LIBDMDUTIL_SHA=33c3c9487247cdced75bdf19a0653b8d7dd14e4a

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

mkdir -p "external/windows-x64/${BUILD_TYPE}"
cd "external/windows-x64/${BUILD_TYPE}"

#
# build SDL3, SDL3_image, SDL3_ttf, SDL3_mixer
#

SDL3_EXPECTED_SHA="${SDL_SHA}-${SDL_IMAGE_SHA}-${SDL_TTF_SHA}-${SDL_MIXER_SHA}"
SDL3_FOUND_SHA="$([ -f SDL3/cache.txt ] && cat SDL3/cache.txt || echo "")"

if [ "${SDL3_EXPECTED_SHA}" != "${SDL3_FOUND_SHA}" ]; then
   echo "Building SDL3. Expected: ${SDL3_EXPECTED_SHA}, Found: ${SDL3_FOUND_SHA}"

   rm -rf SDL3
   mkdir SDL3
   cd SDL3

   curl -sL https://github.com/libsdl-org/SDL/archive/${SDL_SHA}.tar.gz -o SDL-${SDL_SHA}.tar.gz
   tar xzf SDL-${SDL_SHA}.tar.gz
   mv SDL-${SDL_SHA} SDL
   cd SDL
   perl -i -pe 's/OUTPUT_NAME "SDL3"/OUTPUT_NAME "SDL364"/' CMakeLists.txt
   cmake \
      -G "Visual Studio 17 2022" \
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
   perl -i -pe 's/OUTPUT_NAME "SDL3_image"/OUTPUT_NAME "SDL3_image64"/' CMakeLists.txt
   ./external/download.sh
   cmake \
      -G "Visual Studio 17 2022" \
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
   perl -i -pe 's/OUTPUT_NAME SDL3_ttf/OUTPUT_NAME SDL3_ttf64/' CMakeLists.txt
   ./external/download.sh
   cmake \
      -G "Visual Studio 17 2022" \
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
   perl -i -pe 's/OUTPUT_NAME "SDL3_mixer"/OUTPUT_NAME "SDL3_mixer64"/' CMakeLists.txt
   ./external/download.sh
   cmake \
      -G "Visual Studio 17 2022" \
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
FREEIMAGE_FOUND_SHA="$([ -f freeimage/cache.txt ] && cat freeimage/cache.txt || echo "")"

if [ "${FREEIMAGE_EXPECTED_SHA}" != "${FREEIMAGE_FOUND_SHA}" ]; then
   echo "Building FreeImage. Expected: ${FREEIMAGE_EXPECTED_SHA}, Found: ${FREEIMAGE_FOUND_SHA}"

   rm -rf freeimage
   mkdir freeimage
   cd freeimage

   curl -sL https://github.com/toxieainc/freeimage/archive/${FREEIMAGE_SHA}.tar.gz -o freeimage-${FREEIMAGE_SHA}.tar.gz
   tar xzf freeimage-${FREEIMAGE_SHA}.tar.gz
   mv freeimage-${FREEIMAGE_SHA} freeimage
   cd freeimage
   cmake \
      -G "Visual Studio 17 2022" \
      -DPLATFORM=win \
      -DARCH=x64 \
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
BGFX_FOUND_SHA="$([ -f bgfx/cache.txt ] && cat bgfx/cache.txt || echo "")"

if [ "${BGFX_EXPECTED_SHA}" != "${BGFX_FOUND_SHA}" ]; then
   echo "Building BGFX. Expected: ${BGFX_EXPECTED_SHA}, Found: ${BGFX_FOUND_SHA}"

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
   perl -i -pe 's/set_target_properties\(bx PROPERTIES FOLDER "bgfx"\)/set_target_properties(bx PROPERTIES FOLDER "bgfx" OUTPUT_NAME "bx64")/' cmake/bx/bx.cmake
   perl -i -pe 's/set_target_properties\(bimg PROPERTIES FOLDER "bgfx"\)/set_target_properties(bimg PROPERTIES FOLDER "bgfx" OUTPUT_NAME "bimg64")/' cmake/bimg/bimg.cmake
   perl -i -pe 's/set_target_properties\(bimg_decode PROPERTIES FOLDER "bgfx"\)/set_target_properties(bimg_decode PROPERTIES FOLDER "bgfx" OUTPUT_NAME "bimg_decode64")/' cmake/bimg/bimg_decode.cmake
   perl -i -pe 's/set_target_properties\(bimg_encode PROPERTIES FOLDER "bgfx"\)/set_target_properties(bimg_encode PROPERTIES FOLDER "bgfx" OUTPUT_NAME "bimg_encode64")/' cmake/bimg/bimg_encode.cmake
   perl -i -pe 's/set_target_properties\(bgfx PROPERTIES FOLDER "bgfx"\)/set_target_properties(bgfx PROPERTIES FOLDER "bgfx" OUTPUT_NAME "bgfx64")/' cmake/bgfx/bgfx.cmake
   cmake -G "Visual Studio 17 2022" \
      -S. \
      -DBGFX_LIBRARY_TYPE=STATIC \
      -DBGFX_BUILD_TOOLS=OFF \
      -DBGFX_BUILD_EXAMPLES=OFF \
      -DBGFX_CONFIG_MULTITHREADED=ON \
      -DBGFX_CONFIG_MAX_FRAME_BUFFERS=256 \
      -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded \
      -DCMAKE_CXX_FLAGS_RELEASE="//MT" \
      -DCMAKE_C_FLAGS_RELEASE="//MT" \
      -DCMAKE_CXX_FLAGS_DEBUG="//MTd" \
      -DCMAKE_C_FLAGS_DEBUG="//MTd" \
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
PINMAME_FOUND_SHA="$([ -f pinmame/cache.txt ] && cat pinmame/cache.txt || echo "")"

if [ "${PINMAME_EXPECTED_SHA}" != "${PINMAME_FOUND_SHA}" ]; then
   echo "Building libpinmame. Expected: ${PINMAME_EXPECTED_SHA}, Found: ${PINMAME_FOUND_SHA}"

   rm -rf pinmame
   mkdir pinmame
   cd pinmame

   curl -sL https://github.com/vbousquet/pinmame/archive/${PINMAME_SHA}.tar.gz -o pinmame-${PINMAME_SHA}.tar.gz
   tar xzf pinmame-${PINMAME_SHA}.tar.gz
   mv pinmame-${PINMAME_SHA} pinmame
   cd pinmame
   cp cmake/libpinmame/CMakeLists.txt .
   cmake \
      -G "Visual Studio 17 2022" \
      -DPLATFORM=win \
      -DARCH=x64 \
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
OPENXR_FOUND_SHA="$([ -f openxr/cache.txt ] && cat openxr/cache.txt || echo "")"

if [ "${OPENXR_EXPECTED_SHA}" != "${OPENXR_FOUND_SHA}" ]; then
   echo "Building OpenXR. Expected: ${OPENXR_EXPECTED_SHA}, Found: ${OPENXR_FOUND_SHA}"

   rm -rf openxr
   mkdir openxr
   cd openxr

   curl -sL https://github.com/KhronosGroup/OpenXR-SDK-Source/archive/${OPENXR_SHA}.tar.gz -o OpenXR-SDK-Source-${OPENXR_SHA}.zip
   tar xzf OpenXR-SDK-Source-${OPENXR_SHA}.zip
   mv OpenXR-SDK-Source-${OPENXR_SHA} openxr
   cd openxr
   perl -i -pe 's/set_target_properties\(openxr_loader PROPERTIES FOLDER \${LOADER_FOLDER}\)/set_target_properties(openxr_loader PROPERTIES FOLDER \${LOADER_FOLDER} OUTPUT_NAME "openxr_loader64")/' src/loader/CMakeLists.txt
   perl -i -pe 's|\$\{CMAKE_CURRENT_BINARY_DIR\}/\$<CONFIGURATION>/openxr_loader|\${CMAKE_CURRENT_BINARY_DIR}/\$<CONFIGURATION>/openxr_loader64|g' src/loader/CMakeLists.txt
   cmake \
      -G "Visual Studio 17 2022" \
      -DBUILD_TESTS=OFF \
      -DDYNAMIC_LOADER=ON \
      -DOPENXR_DEBUG_POSTFIX="" \
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
LIBDMDUTIL_FOUND_SHA="$([ -f libdmdutil/cache.txt ] && cat libdmdutil/cache.txt || echo "")"

if [ "${LIBDMDUTIL_EXPECTED_SHA}" != "${LIBDMDUTIL_FOUND_SHA}" ]; then
   echo "Building libdmdutil. Expected: ${LIBDMDUTIL_EXPECTED_SHA}, Found: ${LIBDMDUTIL_FOUND_SHA}"

   rm -rf libdmdutil
   mkdir libdmdutil
   cd libdmdutil

   curl -sL https://github.com/vpinball/libdmdutil/archive/${LIBDMDUTIL_SHA}.tar.gz -o libdmdutil-${LIBDMDUTIL_SHA}.tar.gz
   tar xzf libdmdutil-${LIBDMDUTIL_SHA}.tar.gz
   mv libdmdutil-${LIBDMDUTIL_SHA} libdmdutil
   cd libdmdutil
   ./platforms/win/x64/external.sh
   cmake \
      -G "Visual Studio 17 2022" \
      -DPLATFORM=win \
      -DARCH=x64 \
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

cp SDL3/SDL/build/${BUILD_TYPE}/SDL364.lib ../../../third-party/build-libs/windows-x64
cp SDL3/SDL/build/${BUILD_TYPE}/SDL364.dll ../../../third-party/runtime-libs/windows-x64
cp -r SDL3/SDL/include/SDL3 ../../../third-party/include/

cp SDL3/SDL_image/build/${BUILD_TYPE}/SDL3_image64.lib ../../../third-party/build-libs/windows-x64
cp SDL3/SDL_image/build/${BUILD_TYPE}/SDL3_image64.dll ../../../third-party/runtime-libs/windows-x64
cp -r SDL3/SDL_image/include/SDL3_image ../../../third-party/include/

cp SDL3/SDL_ttf/build/${BUILD_TYPE}/SDL3_ttf64.lib ../../../third-party/build-libs/windows-x64
cp SDL3/SDL_ttf/build/${BUILD_TYPE}/SDL3_ttf64.dll ../../../third-party/runtime-libs/windows-x64
cp -r SDL3/SDL_ttf/include/SDL3_ttf ../../../third-party/include/

cp SDL3/SDL_mixer/build/${BUILD_TYPE}/SDL3_mixer64.lib ../../../third-party/build-libs/windows-x64
cp SDL3/SDL_mixer/build/${BUILD_TYPE}/SDL3_mixer64.dll ../../../third-party/runtime-libs/windows-x64
cp -r SDL3/SDL_mixer/include/SDL3_mixer ../../../third-party/include/

cp freeimage/freeimage/build/${BUILD_TYPE}/freeimage64.lib ../../../third-party/build-libs/windows-x64
cp freeimage/freeimage/build/${BUILD_TYPE}/freeimage64.dll ../../../third-party/runtime-libs/windows-x64
cp freeimage/freeimage/Source/FreeImage.h ../../../third-party/include

cp bgfx/bgfx.cmake/build/cmake/bgfx/${BUILD_TYPE}/bgfx64.lib ../../../third-party/build-libs/windows-x64
cp -r bgfx/bgfx.cmake/bgfx/include/bgfx ../../../third-party/include/
cp bgfx/bgfx.cmake/build/cmake/bimg/${BUILD_TYPE}/bimg64.lib ../../../third-party/build-libs/windows-x64
cp bgfx/bgfx.cmake/build/cmake/bimg/${BUILD_TYPE}/bimg_decode64.lib ../../../third-party/build-libs/windows-x64
cp bgfx/bgfx.cmake/build/cmake/bimg/${BUILD_TYPE}/bimg_encode64.lib ../../../third-party/build-libs/windows-x64
cp -r bgfx/bgfx.cmake/bimg/include/bimg ../../../third-party/include/
cp bgfx/bgfx.cmake/build/cmake/bx/${BUILD_TYPE}/bx64.lib ../../../third-party/build-libs/windows-x64
cp -r bgfx/bgfx.cmake/bx/include/bx ../../../third-party/include/

cp pinmame/pinmame/build/${BUILD_TYPE}/pinmame64.lib ../../../third-party/build-libs/windows-x64
cp pinmame/pinmame/build/${BUILD_TYPE}/pinmame64.dll ../../../third-party/runtime-libs/windows-x64
cp pinmame/pinmame/src/libpinmame/libpinmame.h ../../../third-party/include
cp pinmame/pinmame/src/libpinmame/pinmamedef.h ../../../third-party/include

cp openxr/openxr/build/src/loader/${BUILD_TYPE}/openxr_loader64.lib ../../../third-party/build-libs/windows-x64
cp openxr/openxr/build/src/loader/${BUILD_TYPE}/openxr_loader64.dll ../../../third-party/runtime-libs/windows-x64
cp -r openxr/openxr/include/openxr ../../../third-party/include

cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/dmdutil64.lib ../../../third-party/build-libs/windows-x64
cp libdmdutil/libdmdutil/build/${BUILD_TYPE}/dmdutil64.dll ../../../third-party/runtime-libs/windows-x64
cp -r libdmdutil/libdmdutil/include/DMDUtil ../../../third-party/include/
cp libdmdutil/libdmdutil/third-party/build-libs/win/x64/zedmd64.lib ../../../third-party/build-libs/windows-x64
cp libdmdutil/libdmdutil/third-party/runtime-libs/win/x64/zedmd64.dll ../../../third-party/runtime-libs/windows-x64
cp libdmdutil/libdmdutil/third-party/include/ZeDMD.h ../../../third-party/include
cp libdmdutil/libdmdutil/third-party/build-libs/win/x64/serum64.lib ../../../third-party/build-libs/windows-x64
cp libdmdutil/libdmdutil/third-party/runtime-libs/win/x64/serum64.dll ../../../third-party/runtime-libs/windows-x64
cp libdmdutil/libdmdutil/third-party/include/serum.h ../../../third-party/include
cp libdmdutil/libdmdutil/third-party/include/serum-decode.h ../../../third-party/include
cp libdmdutil/libdmdutil/third-party/build-libs/win/x64/libserialport64.lib ../../../third-party/build-libs/windows-x64
cp libdmdutil/libdmdutil/third-party/runtime-libs/win/x64/libserialport64.dll ../../../third-party/runtime-libs/windows-x64
cp libdmdutil/libdmdutil/third-party/build-libs/win/x64/pupdmd64.lib ../../../third-party/build-libs/windows-x64
cp libdmdutil/libdmdutil/third-party/runtime-libs/win/x64/pupdmd64.dll ../../../third-party/runtime-libs/windows-x64
cp libdmdutil/libdmdutil/third-party/include/pupdmd.h ../../../third-party/include
cp libdmdutil/libdmdutil/third-party/build-libs/win/x64/sockpp64.lib ../../../third-party/build-libs/windows-x64
cp libdmdutil/libdmdutil/third-party/runtime-libs/win/x64/sockpp64.dll ../../../third-party/runtime-libs/windows-x64
cp libdmdutil/libdmdutil/third-party/build-libs/win/x64/cargs64.lib ../../../third-party/build-libs/windows-x64
cp libdmdutil/libdmdutil/third-party/runtime-libs/win/x64/cargs64.dll ../../../third-party/runtime-libs/windows-x64