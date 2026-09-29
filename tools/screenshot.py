#!/usr/bin/env python3
"""
Screenshot tool for Wio L1 Tracker Pro (OLED SH1106 and e-ink GxEPD2).
Connects via USB serial using mesh companion protocol, captures framebuffer, saves as PNG.

Usage:
    python screenshot.py [--port PORT] [--scale SCALE]

Options:
    --port PORT    Serial port to use (default: auto-detect)
    --scale SCALE  Upscale factor: 1=no upscale, 2=2x, 3=3x, 4=4x, etc. (default: 1)
"""

import argparse
import serial
import serial.tools.list_ports
from datetime import datetime
from PIL import Image, ImageDraw
import sys
import time
import os

# Mesh protocol constants
FRAME_START_OUT = 0x3C  # '<' - used when sending TO device
FRAME_START_IN = 0x3E  # '>' - used when receiving FROM device
CMD_GET_SCREENSHOT = 66
RESP_CODE_SCREENSHOT = 29
RESP_CODE_ERR = 1

# display_type values (byte [5] in response frame)
DISPLAY_TYPE_OLED = 0   # page-based, column-major (SH1106/SSD1306)
DISPLAY_TYPE_EINK = 1   # row-major, MSB-first, 1=white/0=black (GxEPD2)
DISPLAY_TYPE_RGB565 = 2 # row-major, 16-bit little-endian (colour screens, e.g. Wio Tracker L2)

# No panel-specific constants needed — the firmware now sends GxEPD2's own reported
# dimensions (which use WIDTH_VISIBLE, not the full physical WIDTH), so the header
# already carries the correct visible height and width for any panel/rotation.


def parse_args():
    parser = argparse.ArgumentParser(
        description="Capture screenshots from Wio L1 Tracker Pro (OLED or e-ink)"
    )
    parser.add_argument(
        "--port", "-p", help="Serial port to use (default: auto-detect)"
    )
    parser.add_argument(
        "--scale",
        "-s",
        type=int,
        default=1,
        help="Upscale factor: 1=no upscale, 2=2x, 3=3x, 4=4x, etc. (default: 1)",
    )
    return parser.parse_args()


def find_wio_port():
    """Find Wio Tracker L1 serial port"""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        desc = str(port.description).lower()
        if "wio" in desc or "seeed" in desc:
            return port.device

    # Fallback: check common port names
    for port in ports:
        if "ACM" in port.device or port.device.startswith("COM"):
            return port.device
    return None


def read_frame(ser):
    """Read a complete frame from the mesh protocol (from device)"""
    header = ser.read(3)
    if len(header) != 3:
        return None

    if header[0] != FRAME_START_IN:
        print(
            f"Error: Invalid frame start byte: {header[0]:02x} (expected {FRAME_START_IN:02x})"
        )
        return None

    frame_len = header[1] | (header[2] << 8)

    if frame_len > 0:
        data = bytearray()
        while len(data) < frame_len:
            chunk = ser.read(frame_len - len(data))
            if not chunk:
                print(f"Error: Timeout waiting for frame data")
                return None
            data.extend(chunk)
        return bytes(data)
    else:
        return b""


def send_command(ser, command, data=b""):
    """Send a command frame using mesh protocol (to device)"""
    frame_len = len(data) + 1
    header = bytes([FRAME_START_OUT, frame_len & 0xFF, (frame_len >> 8) & 0xFF])
    frame = header + bytes([command]) + data
    ser.write(frame)
    ser.flush()
    return frame_len + 3


HEADER_SIZE = 11   # see firmware sendScreenshotResponse() for layout


def _u16le(buf, off):
    return buf[off] | (buf[off + 1] << 8)


def _expected_buffer_size(width, height, display_type):
    """Bytes the firmware should send for given dims+type."""
    if display_type == DISPLAY_TYPE_RGB565:
        return width * height * 2
    if display_type == DISPLAY_TYPE_EINK:
        # GxEPD2 buffer is in physical panel coordinates: row-major,
        # stride = ceil(WIDTH_VISIBLE / 8) bytes, HEIGHT rows.
        # WIDTH_VISIBLE = shorter visible dim regardless of rotation.
        visible_short = min(width, height)
        phys_stride   = (visible_short + 7) // 8
        phys_height   = max(width, height)
        return phys_stride * phys_height
    # OLED: packed bits, no padding
    return (width * height) // 8


def receive_screenshot(ser, timeout=5):
    """Receive a multi-chunk screenshot response. Returns (buf, w, h, type, rot) or None."""
    start_time      = time.time()
    buffer_data     = bytearray()
    width           = None
    height          = None
    display_type    = None
    disp_rotation   = 0
    total_chunks    = None
    received_chunks = 0

    while time.time() - start_time < timeout:
        frame = read_frame(ser)
        if frame is None:
            continue
        if len(frame) < 1:
            continue

        resp_code = frame[0]

        if resp_code == RESP_CODE_ERR:
            err = frame[1] if len(frame) > 1 else 0xFF
            print(f"Error: Device returned error code 0x{err:02x}")
            return None

        if resp_code != RESP_CODE_SCREENSHOT:
            continue

        if len(frame) < HEADER_SIZE:
            print(f"Error: Screenshot frame too short: {len(frame)} bytes (need >= {HEADER_SIZE})")
            return None

        disp_type  = frame[1]
        rotation   = frame[2]
        w          = _u16le(frame, 3)
        h          = _u16le(frame, 5)
        chunk_idx  = _u16le(frame, 7)
        num_chunks = _u16le(frame, 9)
        chunk_data = frame[HEADER_SIZE:]

        if chunk_idx == 0:
            width           = w
            height          = h
            display_type    = disp_type
            disp_rotation   = rotation
            total_chunks    = num_chunks
            buffer_data     = bytearray()
            received_chunks = 0

        if width is None:
            print(f"Error: Missed chunk 0 (first chunk seen: {chunk_idx})")
            return None
        if width != w or height != h:
            print(f"Error: Inconsistent dimensions in chunk {chunk_idx}")
            return None
        if total_chunks != num_chunks:
            print(f"Error: Inconsistent chunk count in chunk {chunk_idx}")
            return None

        buffer_data.extend(chunk_data)
        received_chunks += 1

        if received_chunks >= total_chunks:
            expected = _expected_buffer_size(width, height, display_type)
            if len(buffer_data) != expected:
                print(f"Error: Expected {expected} bytes, got {len(buffer_data)}")
                return None
            return bytes(buffer_data), width, height, display_type, disp_rotation

    print(
        f"Error: Timeout waiting for screenshot (received {received_chunks}/{total_chunks or '?'} chunks)"
    )
    return None


def oled_buffer_to_image(buffer, width, height):
    """Decode SH1106/SSD1306 framebuffer (page-based, column-major).
    Each byte covers 8 vertical pixels; bit 0 = top pixel of the byte.
    """
    image = Image.new("1", (width, height), 0)
    pixels = image.load()
    bytes_per_page = width
    for page in range(0, height, 8):
        page_start = (page // 8) * bytes_per_page
        for col in range(width):
            byte_idx = page_start + col
            if byte_idx < len(buffer):
                byte_val = buffer[byte_idx]
                for bit in range(8):
                    if page + bit < height:
                        pixels[col, page + bit] = 1 if (byte_val & (1 << bit)) else 0
    return image.convert("RGB")


def eink_buffer_to_image(buffer, log_width, log_height, rotation):
    """Decode GxEPD2 framebuffer for any panel and rotation (0-3).

    The firmware sends GxEPD2's own width()/height() in the header (uses WIDTH_VISIBLE,
    not the full physical WIDTH) and the GxEPD2 rotation value.

    Buffer layout (always in physical panel coordinates, rotation-independent):
      row-major, stride = GxEPD2_Type::WIDTH / 8 bytes, MSB-first
      byte_idx = phys_x // 8 + phys_y * stride
      bit      = 7 - phys_x % 8
      1 = white (paper), 0 = black (ink)

    Coordinate mapping from GxEPD2 drawPixel (WIDTH = WIDTH_VISIBLE from GFX init):
      rot 0: phys_x = lx,           phys_y = ly
      rot 1: phys_x = WIDTH-1-ly,   phys_y = lx          (WIDTH = log_height)
      rot 2: phys_x = WIDTH-1-lx,   phys_y = HEIGHT-1-ly  (WIDTH = log_width, HEIGHT = log_height)
      rot 3: phys_x = ly,           phys_y = HEIGHT-1-lx  (HEIGHT = log_width)

    Physical HEIGHT (panel's longer dimension) = max(log_w, log_h) for any rotation.
    stride = buffer_size // HEIGHT  (no panel-specific constants needed).
    """
    if log_width == 0 or log_height == 0:
        return Image.new("RGB", (1, 1), (255, 255, 255))

    # Physical HEIGHT is always the longer dimension (panel HEIGHT regardless of rotation).
    phys_height = max(log_width, log_height)
    phys_stride = len(buffer) // phys_height    # bytes per physical row (e.g. 16 for 128-wide panel)

    # vis_w / vis_h are the GFX WIDTH / HEIGHT constants (from GxEPD2's Adafruit_GFX init).
    # For odd rotations GFX swaps them, so we reverse-swap to get the physical sizes.
    if rotation & 1:            # odd: GFX was initialized (WIDTH_VIS, HEIGHT) but swapped
        vis_w = log_height      # = GFX WIDTH  = WIDTH_VISIBLE (e.g. 122)
        vis_h = log_width       # = GFX HEIGHT = physical HEIGHT (e.g. 250)
    else:                       # even: no swap
        vis_w = log_width       # = GFX WIDTH  = WIDTH_VISIBLE
        vis_h = log_height      # = GFX HEIGHT = physical HEIGHT

    image  = Image.new("RGB", (log_width, log_height), (255, 255, 255))
    pixels = image.load()

    for ly in range(log_height):
        for lx in range(log_width):
            if rotation == 0:
                phys_x = lx
                phys_y = ly
            elif rotation == 1:
                phys_x = vis_w - 1 - ly   # = log_height - 1 - ly
                phys_y = lx
            elif rotation == 2:
                phys_x = vis_w - 1 - lx   # = log_width  - 1 - lx
                phys_y = vis_h - 1 - ly   # = log_height - 1 - ly
            else:                          # rotation == 3
                phys_x = ly
                phys_y = vis_h - 1 - lx   # = log_width  - 1 - lx

            byte_idx = phys_x // 8 + phys_y * phys_stride
            bit      = 7 - phys_x % 8
            if byte_idx < len(buffer):
                raw   = (buffer[byte_idx] >> bit) & 1
                color = (255, 255, 255) if raw else (0, 0, 0)
                pixels[lx, ly] = color

    return image


def rgb565_buffer_to_image(buffer, width, height):
    """Colour screen: RGB565 little-endian, row-major."""
    return Image.frombuffer("RGB", (width, height), bytes(buffer), "raw", "BGR;16", 0, 1)


def buffer_to_image(buffer, width, height, display_type, rotation=0, scale=1):
    """Convert framebuffer to PIL Image, optionally upscaled."""
    if display_type == DISPLAY_TYPE_RGB565:
        image_rgb = rgb565_buffer_to_image(buffer, width, height)
    elif display_type == DISPLAY_TYPE_EINK:
        image_rgb = eink_buffer_to_image(buffer, width, height, rotation)
    else:
        image_rgb = oled_buffer_to_image(buffer, width, height)

    if scale > 1:
        image_rgb = image_rgb.resize(
            (image_rgb.width * scale, image_rgb.height * scale),
            Image.Resampling.NEAREST,
        )

    return image_rgb


def generate_filename():
    """Generate timestamp filename in local time with seconds"""
    now = datetime.now()
    os.makedirs("tools/pngs", exist_ok=True)
    return os.path.join("tools/pngs", now.strftime("screenshot_%Y-%m-%d_%H-%M-%S.png"))


def main():
    args = parse_args()

    port = args.port
    if not port:
        port = find_wio_port()

    if not port:
        print("Error: Could not find Wio Tracker L1 serial port")
        print("\nAvailable ports:")
        for p in serial.tools.list_ports.comports():
            print(f"  {p.device}: {p.description}")

        port = input("\nEnter serial port manually: ")

    print(f"\nConnecting to {port} at 115200 baud (scale: {args.scale}x)...")

    try:
        ser = serial.Serial(port, 115200, timeout=3)
        print("Connected!\n")
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        sys.exit(1)

    try:
        print("Screenshot Tool for Wio L1 Tracker Pro (OLED + e-ink)")
        print("Press 'S' to capture screenshot, 'Q' to quit\n")

        while True:
            try:
                key = input("> ").strip().upper()
            except (KeyboardInterrupt, EOFError):
                print("\nExiting...")
                break

            if key == "Q":
                print("Exiting...")
                break

            if key == "S":
                print("Requesting screenshot...")

                ser.reset_input_buffer()
                send_command(ser, CMD_GET_SCREENSHOT)
                time.sleep(0.1)

                result = receive_screenshot(ser)
                if result:
                    buffer_data, width, height, display_type, disp_rotation = result
                    type_str = {DISPLAY_TYPE_EINK: "e-ink", DISPLAY_TYPE_RGB565: "colour"}.get(display_type, "OLED")
                    print(
                        f"Received framebuffer: {width}x{height} {type_str} rot={disp_rotation}, {len(buffer_data)} bytes"
                    )

                    try:
                        image = buffer_to_image(
                            buffer_data, width, height, display_type,
                            rotation=disp_rotation, scale=args.scale
                        )
                        filename = generate_filename()
                        image.save(filename)
                        print(f"Saved as {filename} ({image.width}x{image.height})")
                    except Exception as e:
                        print(f"Error saving image: {e}")
                else:
                    print("Error: Failed to receive screenshot")
            else:
                print("Unknown command. Press 'S' for screenshot, 'Q' to quit")

    except KeyboardInterrupt:
        print("\nExiting...")

    finally:
        ser.close()
        print("Connection closed.")


if __name__ == "__main__":
    main()
