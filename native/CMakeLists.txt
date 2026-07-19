cmake_minimum_required(VERSION 3.18)
project(facephys_native LANGUAGES CXX)

option(BUILD_TFLITE_APPS "Build programs requiring a TensorFlow Lite install" ON)
option(USE_OPENCV "Enable OpenCV video-file support" OFF)
option(ENABLE_TFT "Build TFT tools and enable the TFT application backend" ON)
option(ENABLE_V4L2 "Build V4L2 capture support" ON)
set(TFLITE_ROOT "" CACHE PATH "Prefix or build tree containing TensorFlow Lite/LiteRT")
set(TFLITE_INCLUDE_DIR "" CACHE PATH "TensorFlow Lite header root (needed with an uninstalled shared build)")
set(TFLITE_SOURCE_DIR "" CACHE PATH "TensorFlow source root (supplies non-installed delegate headers)")
set(FACEPHYS_MODEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../models" CACHE PATH "FacePhys model directory")

# Debian/Ubuntu multiarch cross toolchains place target development libraries
# in /usr/lib/<triplet>, outside the traditional /usr/<triplet> sysroot.  Keep
# these hints target-specific and disable CMake's root-prefixing for them so an
# armhf application never accidentally links an x86_64 library.
set(FACEPHYS_MULTIARCH_LIB_HINTS
  "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
  "/usr/lib/arm-linux-gnueabihf")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(Threads REQUIRED)

add_library(facephys_base STATIC
  src/application_state.cpp
  src/camera.cpp
  src/config.cpp
  src/gpio_line.cpp
  src/image_io.cpp
  src/image_preprocess.cpp
  src/log.cpp
  src/st7735.cpp
  src/tft_display.cpp)
target_include_directories(facephys_base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(facephys_base PUBLIC Threads::Threads)
target_compile_options(facephys_base PRIVATE -Wall -Wextra -Wpedantic)
if(USE_OPENCV)
  find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio imgcodecs)
  target_link_libraries(facephys_base PUBLIC ${OpenCV_LIBS})
  target_compile_definitions(facephys_base PUBLIC FACEPHYS_USE_OPENCV=1)
endif()

add_executable(test_ring_buffer tests/test_ring_buffer.cpp)
target_link_libraries(test_ring_buffer PRIVATE facephys_base)
add_executable(test_rgb_resize tests/test_rgb_resize.cpp)
target_link_libraries(test_rgb_resize PRIVATE facephys_base)
add_executable(test_state_mapping tests/test_state_mapping.cpp)
target_link_libraries(test_state_mapping PRIVATE facephys_base)
if(BUILD_TESTING)
  add_test(NAME ring_buffer COMMAND test_ring_buffer)
  add_test(NAME rgb_resize COMMAND test_rgb_resize)
  add_test(NAME state_mapping COMMAND test_state_mapping)
endif()

if(BUILD_TFLITE_APPS)
  # Prefer a shared runtime explicitly.  TensorFlow Lite 2.18's static CMake
  # export is incomplete when it builds XNNPACK itself; using the shared
  # library keeps its exact generated dependency closure intact.  This also
  # supports a TFLite build tree without a fragile `cmake --install` step.
  find_path(FACEPHYS_TFLITE_INCLUDE_DIR tensorflow/lite/interpreter.h HINTS
    "${TFLITE_INCLUDE_DIR}" "${TFLITE_SOURCE_DIR}" "${TFLITE_ROOT}/include" "${TFLITE_ROOT}"
    NO_CMAKE_FIND_ROOT_PATH)
  # CMake-built TFLite calls this libtensorflow-lite.so; the official Bazel
  # target //tensorflow/lite:tensorflowlite emits libtensorflowlite.so.
  # Accept both so the application can link the exact runtime configuration
  # used by benchmark_model rather than a separately configured CMake build.
  # Do not retain a prior configure's CMake-vs-Bazel path.  TFLITE_ROOT is a
  # user-facing build selection and changing it must always change the linked
  # runtime without requiring the caller to delete the build directory.
  unset(TFLITE_SHARED_LIBRARY CACHE)
  unset(TFLITE_SHARED_LIBRARY)
  find_library(TFLITE_SHARED_LIBRARY NAMES
    tensorflow-lite libtensorflow-lite.so tensorflowlite libtensorflowlite.so HINTS
    "${TFLITE_ROOT}/lib" "${TFLITE_ROOT}" NO_CMAKE_FIND_ROOT_PATH)
  get_filename_component(TFLITE_LIBRARY_SUFFIX "${TFLITE_SHARED_LIBRARY}" EXT)
  if(FACEPHYS_TFLITE_INCLUDE_DIR AND TFLITE_SHARED_LIBRARY AND NOT TFLITE_LIBRARY_SUFFIX STREQUAL ".a")
    add_library(facephys_tflite_shared SHARED IMPORTED GLOBAL)
    set_target_properties(facephys_tflite_shared PROPERTIES
      IMPORTED_LOCATION "${TFLITE_SHARED_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${FACEPHYS_TFLITE_INCLUDE_DIR};${TFLITE_SOURCE_DIR};${TFLITE_INCLUDE_DIR}")
    set(FACEPHYS_TFLITE_TARGET facephys_tflite_shared)
    message(WARNING "Using shared TensorFlow Lite fallback: ${TFLITE_SHARED_LIBRARY}")
  else()
    # A distributor-provided CMake package can still be used, including a
    # static package that exports all of its dependency targets.
    if(TFLITE_ROOT)
      list(PREPEND CMAKE_PREFIX_PATH "${TFLITE_ROOT}")
    endif()
    find_package(tensorflow-lite CONFIG REQUIRED HINTS
      "${TFLITE_ROOT}" "${TFLITE_ROOT}/lib/cmake" "${TFLITE_ROOT}/lib/cmake/tensorflow-lite")
    if(TARGET tensorflow-lite::tensorflow-lite)
      set(FACEPHYS_TFLITE_TARGET tensorflow-lite::tensorflow-lite)
    elseif(TARGET tensorflow-lite)
      set(FACEPHYS_TFLITE_TARGET tensorflow-lite)
    else()
      message(FATAL_ERROR "tensorflow-lite package did not export a TensorFlow Lite target")
    endif()
  endif()
  message(STATUS "TensorFlow Lite target: ${FACEPHYS_TFLITE_TARGET}")
  message(STATUS "FacePhys models: ${FACEPHYS_MODEL_DIR}")

  find_path(FACEPHYS_ZLIB_INCLUDE_DIR zlib.h HINTS /usr/include NO_CMAKE_FIND_ROOT_PATH)
  find_library(FACEPHYS_ZLIB_LIBRARY NAMES z libz.so HINTS ${FACEPHYS_MULTIARCH_LIB_HINTS}
    NO_CMAKE_FIND_ROOT_PATH)
  if(NOT FACEPHYS_ZLIB_INCLUDE_DIR OR NOT FACEPHYS_ZLIB_LIBRARY)
    message(FATAL_ERROR "Target zlib development files not found; install zlib1g-dev for the target ABI")
  endif()
  add_library(facephys_zlib UNKNOWN IMPORTED GLOBAL)
  set_target_properties(facephys_zlib PROPERTIES
    IMPORTED_LOCATION "${FACEPHYS_ZLIB_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${FACEPHYS_ZLIB_INCLUDE_DIR}")
  add_library(facephys_runtime STATIC
    src/tflite_model.cpp
    src/facephys_engine.cpp
    src/face_detector.cpp
    src/signal_processor.cpp)
  target_include_directories(facephys_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(facephys_runtime PUBLIC facephys_base ${FACEPHYS_TFLITE_TARGET} facephys_zlib ${CMAKE_DL_LIBS} rt)
  target_compile_options(facephys_runtime PRIVATE -Wall -Wextra -Wpedantic)

  add_executable(inspect_tflite_models tools/inspect_tflite_models.cpp)
  target_link_libraries(inspect_tflite_models PRIVATE facephys_runtime)
  add_executable(run_offline_test tools/run_offline_test.cpp)
  target_link_libraries(run_offline_test PRIVATE facephys_runtime)
  if(ENABLE_V4L2)
    add_executable(facephys_native src/main.cpp)
    target_link_libraries(facephys_native PRIVATE facephys_runtime)
  endif()
endif()

if(ENABLE_V4L2)
  target_sources(facephys_base PRIVATE src/v4l2_camera.cpp src/openmv_serial_camera.cpp)
  target_compile_definitions(facephys_base PUBLIC FACEPHYS_ENABLE_V4L2=1)
  find_path(FACEPHYS_JPEG_INCLUDE_DIR jpeglib.h HINTS /usr/include NO_CMAKE_FIND_ROOT_PATH)
  find_library(FACEPHYS_JPEG_LIBRARY NAMES jpeg libjpeg.so HINTS ${FACEPHYS_MULTIARCH_LIB_HINTS}
    NO_CMAKE_FIND_ROOT_PATH)
  if(FACEPHYS_JPEG_INCLUDE_DIR AND FACEPHYS_JPEG_LIBRARY)
    add_library(facephys_jpeg UNKNOWN IMPORTED GLOBAL)
    set_target_properties(facephys_jpeg PROPERTIES
      IMPORTED_LOCATION "${FACEPHYS_JPEG_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${FACEPHYS_JPEG_INCLUDE_DIR}")
    target_link_libraries(facephys_base PUBLIC facephys_jpeg)
    target_compile_definitions(facephys_base PUBLIC FACEPHYS_HAVE_JPEG=1)
  else()
    message(STATUS "JPEG not found: V4L2 YUYV remains available, MJPEG is disabled")
  endif()
endif()

if(ENABLE_TFT)
  find_path(GPIOD_INCLUDE_DIR gpiod.h HINTS /usr/include NO_CMAKE_FIND_ROOT_PATH)
  find_library(GPIOD_LIBRARY gpiod HINTS ${FACEPHYS_MULTIARCH_LIB_HINTS}
    NO_CMAKE_FIND_ROOT_PATH)
  if(GPIOD_INCLUDE_DIR AND GPIOD_LIBRARY)
    target_include_directories(facephys_base PRIVATE ${GPIOD_INCLUDE_DIR})
    target_link_libraries(facephys_base PUBLIC ${GPIOD_LIBRARY})
    target_compile_definitions(facephys_base PUBLIC FACEPHYS_HAVE_GPIOD=1)
  else()
    message(STATUS "libgpiod not found: ST7735 will use configured sysfs GPIO fallback")
  endif()
endif()

if(ENABLE_V4L2)
  add_executable(test_camera tools/test_camera.cpp)
  target_link_libraries(test_camera PRIVATE facephys_base)
endif()
if(ENABLE_TFT)
  add_executable(test_tft tools/test_tft.cpp)
  target_link_libraries(test_tft PRIVATE facephys_base)
endif()
