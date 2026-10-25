#!/usr/bin/env python3
"""Dependency-free WAV regression measurements for Liberty Riche exports.

Usage:
  python3 Tests/RicheAudioValidation.py --self-test
  python3 Tests/RicheAudioValidation.py --reference dry.wav --render stretched.wav --ratio 1.5 --json results.json
The ratio is output duration / original duration. This script measures audio
exports; it does not render Liberty's engine or replace listening tests.
"""
import argparse
import json
import math
import struct
import wave
from pathlib import Path


def read_wav(path):
    with wave.open(str(path), "rb") as wav:
        channels, rate, width, frames = (wav.getnchannels(), wav.getframerate(),
                                         wav.getsampwidth(), wav.getnframes())
        if channels not in (1, 2) or width not in (2, 3, 4):
            raise ValueError("Expected mono/stereo PCM 16/24/32-bit WAV")
        data = wav.readframes(frames)
    values = [[] for _ in range(channels)]
    for i in range(frames * channels):
        pos = i * width
        if width == 3:
            sample = int.from_bytes(data[pos:pos + 3], "little", signed=True)
        elif width == 2:
            sample = struct.unpack_from("<h", data, pos)[0]
        else:
            sample = struct.unpack_from("<i", data, pos)[0]
        values[i % channels].append(sample / float(1 << (8 * width - 1)))
    return rate, values


def metrics(values, rate, boundary_size):
    result = []
    for channel in values:
        n = len(channel)
        peak = max((abs(v) for v in channel), default=0.0)
        rms = math.sqrt(sum(v * v for v in channel) / n) if n else 0.0
        jumps = [(i, abs(channel[i] - channel[i - 1]))
                 for i in range(1, n)]
        largest = sorted(jumps, key=lambda item: item[1], reverse=True)[:10]
        boundaries = [(i, abs(channel[i] - channel[i - 1]))
                      for i in range(boundary_size, n, boundary_size)]
        result.append({
            "peak": peak, "rms": rms, "clipped_samples": sum(abs(v) >= 1 for v in channel),
            "largest_jumps": largest, "boundary_max_jump": max((v for _, v in boundaries), default=0.0),
            "boundary_positions": sorted(boundaries, key=lambda item: item[1], reverse=True)[:10],
        })
    return result


def analyse(reference, rendered, ratio, boundary):
    ref_rate, ref = read_wav(reference)
    out_rate, out = read_wav(rendered)
    if ref_rate != out_rate:
        raise ValueError("Reference and render sample rates differ")
    if len(ref) != len(out):
        raise ValueError("Reference and render channel counts differ")
    if not ref[0] or not out[0]:
        raise ValueError("Empty audio file")
    actual = len(out[0]) / len(ref[0])
    return {
        "reference": str(reference), "render": str(rendered),
        "sample_rate": ref_rate, "channels": len(out),
        "expected_duration_ratio": ratio, "actual_duration_ratio": actual,
        "duration_ratio_pass": abs(actual - ratio) <= max(0.01, 2 / len(ref[0])),
        "buffer_boundary_samples": boundary,
        "reference_metrics": metrics(ref, ref_rate, boundary),
        "render_metrics": metrics(out, out_rate, boundary),
        "note": "Large jumps can be legitimate attacks; flagged positions require waveform inspection and listening.",
    }


def self_test():
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "test.wav"
        with wave.open(str(p), "wb") as wav:
            wav.setnchannels(2)
            wav.setsampwidth(2)
            wav.setframerate(48000)
            wav.writeframes(b"".join(struct.pack("<hh", int(12000 * math.sin(2 * math.pi * i / 48)),
                                                   int(6000 * math.sin(2 * math.pi * i / 48)))
                                     for i in range(4800)))
        result = analyse(p, p, 1.0, 256)
        assert result["duration_ratio_pass"]
        assert result["channels"] == 2
        assert result["render_metrics"][0]["peak"] > result["render_metrics"][1]["peak"]
        assert result["render_metrics"][0]["clipped_samples"] == 0
        print("Riche WAV analyzer self-test: PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--render", type=Path)
    parser.add_argument("--ratio", type=float, default=1.0)
    parser.add_argument("--boundary", type=int, default=256)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if not args.reference or not args.render or args.ratio <= 0 or args.boundary <= 0:
            parser.error("Specify --reference, --render, positive --ratio and --boundary")
        result = analyse(args.reference, args.render, args.ratio, args.boundary)
        output = json.dumps(result, indent=2)
        if args.json:
            args.json.write_text(output + "\n", encoding="utf-8")
        print(output)
        if not result["duration_ratio_pass"] or any(
            ch["clipped_samples"] for ch in result["render_metrics"]
        ):
            raise SystemExit(1)
