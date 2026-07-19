# OpenMV USB CDC -> FacePhys native packet stream.
#
# Save this as main.py on the OpenMV (or run it from OpenMV IDE) before starting
# native/bin/test_camera or facephys_native on the NanoPi. Do not leave IDE's
# serial terminal attached while the NanoPi is receiving the binary stream.
#
# Packet, little-endian:
#   4s magic "FPMV", uint16 width, uint16 height, uint32 sequence,
#   uint32 capture_time_ms, uint32 jpeg_size, jpeg_size bytes of JPEG data.

import sensor
import struct
import time
import pyb

WIDTH = 320
HEIGHT = 240
JPEG_QUALITY = 35       # Start here for USB full-speed throughput.
TARGET_FPS = 15
FRAME_PERIOD_MS = 1000 // TARGET_FPS

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.set_windowing((WIDTH, HEIGHT))
sensor.skip_frames(time=2000)

usb = pyb.USB_VCP()
usb.setinterrupt(-1)    # Required for a raw binary protocol (no Ctrl-C byte).
sequence = 0

def send_all(buffer):
    offset = 0
    size = len(buffer)
    while offset < size:
        sent = usb.send(memoryview(buffer)[offset:], timeout=1000)
        if sent is None:
            # Older OpenMV firmware may not return a byte count on success.
            sent = size - offset
        if sent <= 0:
            raise OSError("USB VCP send timed out")
        offset += sent

while True:
    start_ms = time.ticks_ms()
    image = sensor.snapshot()
    jpeg = image.compress(quality=JPEG_QUALITY)
    payload_size = len(jpeg)
    header = struct.pack("<4sHHIII", b"FPMV", WIDTH, HEIGHT, sequence,
                         start_ms & 0xFFFFFFFF, payload_size)
    # USB_VCP.isconnected() is IDE-oriented on some OpenMV firmware releases.
    # Instead, retry a failed send until the NanoPi opens ttyACM0.
    try:
        send_all(header)
        send_all(jpeg)
    except OSError:
        time.sleep_ms(100)
        continue
    sequence = (sequence + 1) & 0xFFFFFFFF

    remaining_ms = FRAME_PERIOD_MS - time.ticks_diff(time.ticks_ms(), start_ms)
    if remaining_ms > 0:
        time.sleep_ms(remaining_ms)
