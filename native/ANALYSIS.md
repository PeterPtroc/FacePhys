# FacePhys native-port analysis (2026-07-19)

This document records facts extracted from the checked-in Web demo before the
native implementation was started.  It is deliberately a mapping record, not a
replacement rPPG algorithm.

## Browser call graph

```text
camera video
  -> MediaPipe BlazeFace (every browser frame)
  -> 1-D Kalman box smoothing (q=1e-2, r=5e-1)
  -> height *= 1.2; originY -= height * 0.2
  -> Canvas crop + resize to 36 x 36
  -> RGB / 255.0, NHW C [1,1,36,36,3]
  -> inference_worker model.tflite
       dt in seconds; 46 explicit recurrent-state output-to-input copies
       output 0 = BVP
  -> 450-sample chronological BVP window (zero padded at the start)
  -> sqi_model.tflite and psd_model.tflite
  -> reliable iff SQI > 0.38 and a face was seen within 500 ms
  -> BPM = psd_hr / 30.0 / dt
```

`proj.tflite` only provides browser visualisation tensors.  It is intentionally
not on the native measurement critical path.

## Verified model interfaces

The following was parsed directly from the bundled FlatBuffer models.

* `model.tflite`: 48 float inputs and 47 float outputs.  Input tensor names are
  `input` (`[1,1,36,36,3]`), `dt` (`[1]`), then `state_in_0` through
  `state_in_45`.  Output 0 is `Identity`, `[1,1]`, the BVP.
* `state.gz`: gzip JSON with exactly `state_in_0` through `state_in_45`.  Every
  entry shape and count matches the corresponding main-model state tensor.
* `sqi_model.tflite`: input `[1,450]`, one float scalar output.
* `psd_model.tflite`: input `[1,450]`; its `serving_default` signature maps
  `output_0` to HR (float scalar), `output_1` to frequency (`[1,2730]`),
  `output_2` to PSD (`[1,2730]`), and `output_3` to peak index (int scalar).
  Native code binds these semantic tensor indices, not a coincidental raw
  subgraph-output order.
* `blaze_face_short_range.tflite`: input `[1,128,128,3]`, `regressors`
  `[1,896,16]` and `classificators` `[1,896,1]` outputs.

The Web worker's `getInputDetails()` puts `dt` before `input`, while the raw
TFLite subgraph stores the two tensors in the reverse order.  Native code must
bind by tensor name; it must not copy the worker's two numeric constants.

## Exact recurrent map

`inference_worker.js` is the source of truth. Its 46 pairs are retained as
`kBrowserLiteRtStateMap`. A real C++ `Interpreter` inspection showed that
LiteRT JS reorders its detail arrays: the FlatBuffer itself presents
`state_in_0..45` at C++ positions 2..47 and matching `Identity_1..46` at
positions 1..46. `kNativeTfliteStateMap` is the verified translation used by
the C++ engine; each pair is checked for name, type and byte size before the
first invocation. The source recurrence is unchanged.

## Target facts verified over SSH

* Target: NanoPi NEO, Ubuntu 22.04 armv7l, Linux 4.14.111.
* `/dev/spidev0.0` is currently `root:spi 0660`; `pi` is already in `spi`.
* `/dev/gpiochip0` is `1c20800.pinctrl`, base 0, 224 GPIOs.  Thus the supplied
  TFT lines are `PA6 -> /dev/gpiochip0 line 6` and
  `PG11 -> /dev/gpiochip0 line 203`.
* `gpiodetect`/`gpioinfo` are not installed on the target, so the native GPIO
  wrapper provides a sysfs compatibility backend as well as optional libgpiod.
* OpenMV is USB CDC ACM (`1209:abd1`, `cdc_acm`, `/dev/ttyACM0`), not UVC, so
  there is no `/dev/video*`. The native OpenMV backend consumes the explicit
  JPEG packet protocol in `openmv/openmv_facephys_stream.py`.

## Phased implementation

1. Completed: inspect JS, state JSON, model tensors and GPIO mapping.
2. Main-model engine: named input binding, zlib state loading, direct state
   update and tensor inspection utility.
3. Offline PPM/image sequence test, fixed ROI and CSV output.
4. V4L2 latest-frame capture and threaded realtime application.
5. ST7735 user-space driver plus independent TFT test.
6. Low-rate bundled BlazeFace detector and Kalman ROI tracker.
7. Armhf cross build, deploy, permissions and systemd scripts.  Camera/TFT
   electrical behaviour remains a target-board validation item.
