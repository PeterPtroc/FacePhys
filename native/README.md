# FacePhys native Linux port

This directory is an additive C++17 port.  It leaves the browser Demo,
JavaScript and WASM untouched.  See [ANALYSIS.md](ANALYSIS.md) for the verified
model contracts and the exact recurrent-state provenance.

## Build

The default cross build deliberately uses the same TensorFlow Bazel
configuration as the NanoPi benchmark, then links the produced
`//tensorflow/lite:tensorflowlite` armhf shared library.  This keeps XNNPACK's
delegate graph and CPU kernels aligned with `benchmark_model`:

```bash
cd FacePhys-Demo
TENSORFLOW_SRC=/absolute/path/to/tensorflow-v2.18.1 native/scripts/build_armhf.sh
```

Internally this runs:

```bash
cd /absolute/path/to/tensorflow-v2.18.1
bazel build -c opt --config=elinux_armhf --define=tflite_with_xnnpack=true \
  --jobs=4 //tensorflow/lite:tensorflowlite
```

TensorFlow v2.18.1 requires Bazel 6.5.0. `arm-linux-gnueabihf-g++` is used by
the `elinux_armhf` toolchain. Verify the deployed main model with:

```bash
cd /home/pi/FacePhys-Demo
LD_LIBRARY_PATH=native/bin native/bin/inspect_tflite_models models --xnnpack
```

It must report `XNNPACK=ON` and the expected delegate-kernel count. A CMake
TensorFlow Lite build is still available only as an explicit fallback; it may
select a different XNNPACK graph and therefore must be benchmarked separately:

```bash
TFLITE_BUILD_SYSTEM=cmake TFLITE_ROOT=/opt/tflite-armhf native/scripts/build_armhf.sh
```

In `cmake` fallback mode, the script builds a matching host-only `flatc` from
the same TensorFlow source when needed. To reuse an existing matching compiler,
set `TFLITE_HOST_TOOLS_DIR` to its directory.

For a native x86 developer build, point CMake at an x86 TFLite build tree and
its source/header roots:

```bash
cmake -S native -B build/native-host -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DTFLITE_ROOT=/path/to/tflite-build -DTFLITE_SOURCE_DIR=/path/to/tensorflow \
  -DTFLITE_INCLUDE_DIR=/path/to/tflite-build/flatbuffers/include \
  -DUSE_OPENCV=OFF -DENABLE_TFT=ON -DENABLE_V4L2=ON
cmake --build build/native-host --parallel
ctest --test-dir build/native-host --output-on-failure
```

`USE_OPENCV=OFF` is the default; V4L2 YUYV and PPM offline inputs require no
OpenCV.  libjpeg-turbo is found automatically for MJPEG.  `ENABLE_TFT=OFF` and
`ENABLE_V4L2=OFF` remove those executable backends.

For the currently attached OpenMV, install the target JPEG development/runtime
package and other native application dependencies before building:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build libjpeg-turbo8-dev \
  libgpiod-dev python3-serial rsync
```

## OpenMV USB serial camera

The attached device is an OpenMV USB CDC ACM device, not a UVC webcam, so it
correctly appears as `/dev/ttyACM0` rather than `/dev/video0`.  Copy
[`openmv_facephys_stream.py`](openmv/openmv_facephys_stream.py) to the OpenMV
as `main.py` (or run it from OpenMV IDE). It emits framed QVGA JPEG images over
USB; it must be the only program using the OpenMV serial port. The NanoPi
configuration already selects `camera.backend = "openmv_serial"` and
`/dev/openmv`.

After deployment, install the udev rule, unplug/replug the OpenMV and verify:

```bash
native/scripts/setup_permissions.sh pi
ls -l /dev/openmv /dev/ttyACM0
```

The stream begins at JPEG quality 35 and 15 FPS. If frames time out, first
lower `JPEG_QUALITY` to 25 or `TARGET_FPS` to 10 in the OpenMV script; do not
increase JPEG quality before the stream is stable.

## Deploy and target setup

```bash
native/scripts/deploy_to_nanopi.sh build/native-armhf
ssh pi@10.0.0.243 'cd FacePhys-Demo && native/scripts/setup_permissions.sh pi'
```

SSH keys are preferred. For an already-authorized password-based maintenance
session, the deploy helper also accepts `USE_SSHPASS=1` and an `SSHPASS`
environment variable; the password is never stored in the repository.

Log out/in after changing group membership.  The rule gives `spi` group access
to spidev and gpiochip devices; it does not use `chmod 666` or run the app as
root.

## Verification commands on NanoPi

Run from `/home/pi/FacePhys-Demo`:

```bash
export LD_LIBRARY_PATH="$PWD/native/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
native/bin/inspect_tflite_models models --xnnpack
native/bin/test_tft native/config/nanopi_neo_st7735.json
native/bin/test_tft native/config/nanopi_neo_st7735.json --rotation=1 --x-offset=0 --y-offset=0 --speed=8000000
native/bin/test_camera native/config/nanopi_neo_st7735.json
native/bin/run_offline_test native/config/nanopi_neo_st7735.json /path/to/ppm_frames /tmp/facephys.csv 15 80 40 160 180
native/scripts/run_facephys.sh native/config/nanopi_neo_st7735.json
sudo native/scripts/install_service.sh
sudo systemctl start facephys.service
journalctl -u facephys.service -f
```

`test_tft` cycles solid red/green/blue/white/black, then draws bars, text,
rectangle and waveform.  First confirm 4/8 MHz and adjust only the JSON
`rotation`, `x_offset`, `y_offset`, `bgr`, `invert` and `spi_speed_hz` values.

`run_offline_test` intentionally accepts a P6 or P3 PPM sequence so that model and
state equivalence can be established before camera variation.  It emits:
`timestamp,dt,bvp,sqi,bpm,inference_ms`.

## Threading and measurement policy

* V4L2 capture publishes one latest RGB frame; stalled inference drops frames.
* Main FacePhys uses configured XNNPACK/4 threads.  SQI/PSD each use one
  non-XNNPACK interpreter.  PSD runs at the configured interval and its thread
  has lower Linux nice priority.
* Main model binding uses names (`input`, `dt`, state names), retains the
  `inference_worker.js` map, and applies its verified C++ TFLite ordering
  translation. It rejects NaN/Inf and invalid dt.
* No face causes BVP/SQI/BPM reset; BPM is only displayed when SQI > 0.38.
* The TFT refreshes at `display_interval_ms`, never from the inference thread.

## JS/native consistency procedure

Before clinical use, export identical 36x36 RGB float frames, `dt`, and the
initial `state.gz` state from the browser worker.  Record browser BVP and a
subset of state outputs for N frames.  Feed the same PPM/ROI or direct tensor
fixture into `run_offline_test`, then compare BVP and mapped states with a
small float tolerance.  If they diverge, check RGB order, resize interpolation,
dt and mapping before changing any model or signal logic.

For the final numerical comparison after producing equivalent CSVs, run:

```bash
python3 native/tools/compare_bvp_csv.py browser_bvp.csv /tmp/facephys.csv
```

## Target-board items not claimed as verified

This host build does not prove the electrical ST7735 tab/offset settings, actual
camera format, MJPEG decode path, BlazeFace normalization, temperature, RSS or
long-duration stability.  Use the commands above and record `ps`, `vcgencmd`
(if available), `/sys/class/thermal`, journal logs and a 5--10 minute run on
the actual NanoPi.
