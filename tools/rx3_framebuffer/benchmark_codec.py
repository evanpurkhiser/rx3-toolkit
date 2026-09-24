"""Measure dirty-tile codec choices against RX3-shaped framebuffer changes."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import random
import time
import zlib


WIDTH = 1280
HEIGHT = 800
BYTES_PER_PIXEL = 2


@dataclass(frozen=True)
class Result:
    name: str
    rectangles: int
    raw_bytes: int
    xor_rle_bytes: int
    xor_zlib_bytes: int
    rle_zlib_bytes: int
    rle_lz4_bytes: int
    lz4_ms: float
    elapsed_ms: float


def _tiles(width: int, height: int, tile: int):
    for y in range(0, height, tile):
        for x in range(0, width, tile):
            yield x, y, min(tile, width - x), min(tile, height - y)


def _xor_rle(previous: memoryview, current: memoryview) -> bytes:
    encoded = bytearray()
    pixel_count = len(current) // 2
    pixel = 0
    while pixel < pixel_count:
        offset = pixel * 2
        if previous[offset:offset + 2] == current[offset:offset + 2]:
            run = 1
            while run < 128 and pixel + run < pixel_count:
                at = (pixel + run) * 2
                if previous[at:at + 2] != current[at:at + 2]:
                    break
                run += 1
            encoded.append(run - 1)
        else:
            run = 1
            while run < 128 and pixel + run < pixel_count:
                at = (pixel + run) * 2
                if previous[at:at + 2] == current[at:at + 2]:
                    break
                run += 1
            encoded.append(0x80 | (run - 1))
            for changed in range(pixel, pixel + run):
                at = changed * 2
                value = int.from_bytes(previous[at:at + 2], "little")
                value ^= int.from_bytes(current[at:at + 2], "little")
                encoded.extend(value.to_bytes(2, "little"))
        pixel += run
    return bytes(encoded)


def _lz4_block(data: bytes) -> bytes:
    """Reference encoder matching the small device-side LZ4 parser."""
    table = [-1] * (1 << 14)
    output = bytearray()
    anchor = position = 0

    def write_length(length: int) -> None:
        while length >= 255:
            output.append(255)
            length -= 255
        output.append(length)

    while position + 12 <= len(data):
        sequence = int.from_bytes(data[position:position + 4], "little")
        hashed = (sequence * 2654435761 & 0xFFFFFFFF) >> 18
        reference = table[hashed]
        table[hashed] = position
        if (
            reference < 0
            or position - reference > 65535
            or data[reference:reference + 4] != data[position:position + 4]
        ):
            position += 1
            continue

        match_length = 4
        match_limit = len(data) - 5
        while (
            position + match_length < match_limit
            and data[reference + match_length] == data[position + match_length]
        ):
            match_length += 1
        literal_length = position - anchor
        token_at = len(output)
        output.append(min(literal_length, 15) << 4)
        if literal_length >= 15:
            write_length(literal_length - 15)
        output.extend(data[anchor:position])
        output.extend((position - reference).to_bytes(2, "little"))
        match_code = match_length - 4
        output[token_at] |= min(match_code, 15)
        if match_code >= 15:
            write_length(match_code - 15)
        position += match_length
        anchor = position
        if position >= 2 and position + 2 < len(data):
            insertion = position - 2
            value = int.from_bytes(data[insertion:insertion + 4], "little")
            table[(value * 2654435761 & 0xFFFFFFFF) >> 18] = insertion

    literal_length = len(data) - anchor
    output.append(min(literal_length, 15) << 4)
    if literal_length >= 15:
        write_length(literal_length - 15)
    output.extend(data[anchor:])
    return bytes(output)


def _extract(frame: bytes, width: int, x: int, y: int, w: int, h: int) -> bytes:
    stride = width * BYTES_PER_PIXEL
    row_bytes = w * BYTES_PER_PIXEL
    return b"".join(
        frame[(y + row) * stride + x * 2:(y + row) * stride + x * 2 + row_bytes]
        for row in range(h)
    )


def measure(name: str, previous: bytes, current: bytes, width: int, height: int,
            tile: int) -> Result:
    started = time.perf_counter()
    rectangles = raw_bytes = encoded_bytes = 0
    for x, y, w, h in _tiles(width, height, tile):
        old = _extract(previous, width, x, y, w, h)
        new = _extract(current, width, x, y, w, h)
        if old == new:
            continue
        rectangles += 1
        raw_bytes += len(new) + 12
        encoded_bytes += min(len(new), len(_xor_rle(memoryview(old), memoryview(new)))) + 12
    elapsed_ms = (time.perf_counter() - started) * 1000
    delta = bytes(left ^ right for left, right in zip(previous, current, strict=True))
    full_rle = _xor_rle(memoryview(previous), memoryview(current))
    lz4_started = time.perf_counter()
    lz4 = _lz4_block(full_rle)
    lz4_ms = (time.perf_counter() - lz4_started) * 1000
    return Result(
        name, rectangles, raw_bytes, encoded_bytes,
        len(zlib.compress(delta, 1)), len(zlib.compress(full_rle, 1)),
        len(lz4), lz4_ms, elapsed_ms,
    )


def _paint(frame: bytearray, x: int, y: int, width: int, height: int,
           value: int) -> None:
    pixel = value.to_bytes(2, "little")
    stride = WIDTH * 2
    line = pixel * width
    for row in range(y, min(y + height, HEIGHT)):
        start = row * stride + x * 2
        frame[start:start + len(line)] = line


def scenarios(base: bytes):
    sparse = bytearray(base)
    for index in range(20):
        _paint(sparse, 80 + index * 43, 90 + index % 4 * 30, 7, 14,
               0xFFFF if index & 1 else 0x07E0)
    yield "sparse controls", bytes(sparse)

    waveform = bytearray(base)
    stride = WIDTH * 2
    top, bottom = 260, 540
    for row in range(top, bottom):
        start = row * stride
        line = waveform[start:start + stride]
        waveform[start:start + stride - 2] = line[2:]
        waveform[start + stride - 2:start + stride] = line[-2:]
    yield "waveform 1px scroll", bytes(waveform)

    noisy = bytearray(base)
    generator = random.Random(3)
    for _ in range(200_000):
        pixel = generator.randrange(WIDTH * HEIGHT)
        value = generator.randrange(65536)
        noisy[pixel * 2:pixel * 2 + 2] = value.to_bytes(2, "little")
    yield "200k changed pixels", bytes(noisy)


def downsample(frame: bytes, factor: int) -> bytes:
    output = bytearray((WIDTH // factor) * (HEIGHT // factor) * 2)
    destination = 0
    stride = WIDTH * 2
    for y in range(0, HEIGHT, factor):
        row = memoryview(frame)[y * stride:(y + 1) * stride]
        for x in range(0, WIDTH, factor):
            source = x * 2
            output[destination:destination + 2] = row[source:source + 2]
            destination += 2
    return bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("frame", type=Path, nargs="?", default=Path("/tmp/rx3-fb0.raw"))
    args = parser.parse_args()
    base = args.frame.read_bytes()
    expected = WIDTH * HEIGHT * 2
    if len(base) < expected:
        raise SystemExit(f"need at least {expected} bytes, got {len(base)}")
    base = base[:expected]

    captured = sorted(args.frame.parent.glob("rx3-frame-*.raw"))
    inputs = list(scenarios(base))
    if len(captured) > 1:
        frames = [path.read_bytes()[:expected] for path in captured]
        inputs.extend(
            (f"capture {index - 1}->{index}", frames[index - 1], frames[index])
            for index in range(1, len(frames))
        )

    print("scenario                 scale rects      raw tile-rle xor-zlib rle-zlib  rle-lz4 lz4-ms")
    for entry in inputs:
        if len(entry) == 2:
            name, changed = entry
            previous = base
        else:
            name, previous, changed = entry
        for scale in (1, 2):
            old = previous if scale == 1 else downsample(previous, scale)
            new = changed if scale == 1 else downsample(changed, scale)
            width, height = WIDTH // scale, HEIGHT // scale
            result = measure(name, old, new, width, height, 32)
            print(f"{name:24} {scale:>5} {result.rectangles:>5} "
                  f"{result.raw_bytes:>8,} {result.xor_rle_bytes:>9,} "
                  f"{result.xor_zlib_bytes:>8,} {result.rle_zlib_bytes:>8,} "
                  f"{result.rle_lz4_bytes:>8,} {result.lz4_ms:>6.1f}")


if __name__ == "__main__":
    main()
