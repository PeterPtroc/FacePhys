#!/usr/bin/env bash
set -euo pipefail

native_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$native_dir/.." && pwd)"
tensorflow_src="${TENSORFLOW_SRC:-$repo_dir/../tensorflow}"
tflite_build_system="${TFLITE_BUILD_SYSTEM:-bazel}"
tflite_build="${TFLITE_ROOT:-${TFLITE_BUILD_DIR:-$repo_dir/build/tflite-armhf}}"
app_build="${APP_BUILD_DIR:-$repo_dir/build/native-armhf}"
toolchain="$native_dir/cmake/armhf-toolchain.cmake"
host_tools_dir="${TFLITE_HOST_TOOLS_DIR:-}"
host_tools_build="${TFLITE_HOST_TOOLS_BUILD_DIR:-$repo_dir/build/tflite-host-tools}"

command -v arm-linux-gnueabihf-g++ >/dev/null || { echo "Missing arm-linux-gnueabihf-g++" >&2; exit 1; }
test -f "$toolchain" || { echo "Missing toolchain file: $toolchain" >&2; exit 1; }
if [[ "$tflite_build_system" == "cmake" && -z "$host_tools_dir" ]]; then
  test -f "$tensorflow_src/tensorflow/lite/CMakeLists.txt" || { echo "Set TENSORFLOW_SRC to TensorFlow v2.18.1 source" >&2; exit 1; }
  host_flatc="$host_tools_build/_deps/flatbuffers-build/flatc"
  if [[ ! -x "$host_flatc" ]]; then
    # The generated TFLite/XNNPACK headers must use a flatc version compatible
    # with this TensorFlow source tree. Distribution flatc packages are often
    # too old, so build the small host-only tool once from the same source.
    cmake -S "$tensorflow_src/tensorflow/lite" -B "$host_tools_build" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DTFLITE_ENABLE_XNNPACK=OFF -DTFLITE_ENABLE_INSTALL=OFF
    cmake --build "$host_tools_build" --target flatc --parallel "${BUILD_JOBS:-4}"
  fi
  host_tools_dir="$(dirname "$host_flatc")"
fi

# The benchmark measurements supplied for NanoPi were made with Bazel.  Build
# and link the matching shared-runtime target by default, so XNNPACK's kernel
# selection and compiler options are identical.  CMake remains an explicit
# fallback for environments that cannot use the TensorFlow Bazel workspace.
case "$tflite_build_system" in
  bazel)
    test -f "$tensorflow_src/.bazelversion" || { echo "Set TENSORFLOW_SRC to the TensorFlow v2.18.1 Bazel workspace" >&2; exit 1; }
    command -v bazel >/dev/null || { echo "Missing bazel (TensorFlow v2.18.1 requires Bazel 6.5.0)" >&2; exit 1; }
    (
      cd "$tensorflow_src"
      bazel build -c opt --config=elinux_armhf --define=tflite_with_xnnpack=true \
        --jobs="${BUILD_JOBS:-4}" //tensorflow/lite:tensorflowlite
    )
    tflite_root="$tensorflow_src/bazel-bin/tensorflow/lite"
    tflite_library="$tflite_root/libtensorflowlite.so"
    bazel_output_base="$(cd "$tensorflow_src" && bazel info output_base)"
    tflite_include_dir="$bazel_output_base/external/flatbuffers/include"
    test -f "$tflite_library" || { echo "Bazel did not produce $tflite_library" >&2; exit 1; }
    test -f "$tflite_include_dir/flatbuffers/flatbuffers.h" || {
      echo "Bazel FlatBuffers headers not found: $tflite_include_dir" >&2; exit 1;
    }
    ;;
  cmake)
    test -x "$host_tools_dir/flatc" || { echo "Missing executable $host_tools_dir/flatc" >&2; exit 1; }
    test -f "$tensorflow_src/tensorflow/lite/CMakeLists.txt" || { echo "Set TENSORFLOW_SRC to TensorFlow v2.18.1 source" >&2; exit 1; }
    cmake -S "$tensorflow_src/tensorflow/lite" -B "$tflite_build" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
      -DBUILD_SHARED_LIBS=ON -DTFLITE_ENABLE_XNNPACK=ON -DTFLITE_ENABLE_INSTALL=OFF \
      -DTFLITE_HOST_TOOLS_DIR="$host_tools_dir" \
      -DFLATC_BIN:FILEPATH="$host_tools_dir/flatc" \
      -DXNNPACK_ENABLE_ARM_BF16=OFF -DXNNPACK_ENABLE_ARM_DOTPROD=OFF \
      -DXNNPACK_ENABLE_ARM_FP16_SCALAR=ON -DXNNPACK_ENABLE_ARM_FP16_VECTOR=ON \
      -DXNNPACK_ENABLE_ARM_I8MM=OFF
    cmake --build "$tflite_build" --target tensorflow-lite --parallel "${BUILD_JOBS:-4}"
    tflite_root="$tflite_build"
    tflite_library="$tflite_root/libtensorflow-lite.so"
    tflite_include_dir="$tflite_root/flatbuffers/include"
    test -f "$tflite_include_dir/flatbuffers/flatbuffers.h" || {
      echo "TFLite build is missing generated FlatBuffers headers: $tflite_root" >&2; exit 1;
    }
    ;;
  *)
    echo "TFLITE_BUILD_SYSTEM must be bazel or cmake, got: $tflite_build_system" >&2
    exit 1
    ;;
esac

cmake -S "$native_dir" -B "$app_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
  -DTFLITE_ROOT="$tflite_root" -DTFLITE_SOURCE_DIR="$tensorflow_src" \
  -DTFLITE_INCLUDE_DIR="$tflite_include_dir" \
  -DFACEPHYS_MODEL_DIR="$repo_dir/models" \
  -DUSE_OPENCV=OFF -DENABLE_TFT=ON -DENABLE_V4L2=ON
cmake --build "$app_build" --parallel "${BUILD_JOBS:-4}"

# Keep the generated .so closure alongside the executables.  The target run
# wrapper sets LD_LIBRARY_PATH to native/bin, so no system-wide installation or
# hand-maintained XNNPACK/abseil library list is necessary.
runtime_dir="$app_build/runtime"
mkdir -p "$runtime_dir"
while IFS= read -r -d '' library; do
  install -m 0755 "$library" "$runtime_dir/$(basename "$library")"
done < <(find "$tflite_root" -type f \( -name '*.so' -o -name '*.so.*' \) -print0)
test -f "$runtime_dir/$(basename "$tflite_library")" || {
  echo "Runtime packaging did not find $(basename "$tflite_library")" >&2; exit 1;
}
echo "armhf binaries: $app_build"
