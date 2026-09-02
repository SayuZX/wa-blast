set -euo pipefail

NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK:-}}"
if [ -z "$NDK" ]; then
  echo "[ERROR] Set ANDROID_NDK_HOME to your Android NDK path." >&2
  exit 1
fi

API=28
ABI=arm64-v8a
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
# On macOS the prebuilt host dir differs; auto-detect if needed.
if [ ! -d "$TOOLCHAIN" ] && [ -d "$NDK/toolchains/llvm/prebuilt/darwin-x86_64" ]; then
  TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/darwin-x86_64"
fi

TARGET="aarch64-linux-android$API"
export CC="$TOOLCHAIN/bin/$TARGET-clang"
export CXX="$TOOLCHAIN/bin/$TARGET-clang++"

rm -rf build-android
cmake -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" \
  -DANDROID_PLATFORM="android-$API" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-android --target wa_api_server -j"$(nproc)"

echo
echo "================================================================"
echo " Binary built: build-android/wa_apid"
echo " Deploy:  adb push build-android/wa_apid /data/local/tmp/wa_apid"
echo "          adb shell chmod +x /data/local/tmp/wa_apid"
echo "          adb shell /data/local/tmp/wa_apid"
echo "================================================================"
