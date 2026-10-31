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



def envelope_metrics(channel, rate):
    """Short-time energy variation; descriptive, not a perceptual score."""
    window = max(1, round(rate * 0.01))
    rms = [math.sqrt(sum(x * x for x in channel[i:i + window]) /
                     len(channel[i:i + window]))
           for i in range(0, len(channel), window)]
    changes = [abs(rms[i] - rms[i - 1]) for i in range(1, len(rms))]
    return {"window_ms": 10, "max_rms_step": max(changes, default=0.0),
            "mean_rms_step": sum(changes) / len(changes) if changes else 0.0}


def transient_metrics(channel, rate):
    """Measure energy immediately after strongest attack-like derivative."""
    if len(channel) < 3:
        return {"attack_position": None, "attack_peak": 0.0}
    index = max(range(1, len(channel)), key=lambda i: abs(channel[i] - channel[i - 1]))
    end = min(len(channel), index + max(1, round(rate * 0.01)))
    return {"attack_position": index,
            "attack_peak": max(abs(x) for x in channel[index:end])}


def boundary_anomaly(channel, block):
    """Flag boundary jumps compared with neighboring non-boundary derivatives.

    Robust ratio alone is not a definitive audible-click classifier.
    """
    jumps = [abs(channel[i] - channel[i - 1]) for i in range(1, len(channel))]
    if not jumps:
        return {"max_ratio": 0.0, "suspect_positions": []}
    sorted_jumps = sorted(jumps)
    baseline = sorted_jumps[len(sorted_jumps) // 2]
    floor = max(0.002, baseline * 25)
    suspects = []
    for i in range(block, len(channel), block):
        delta = jumps[i - 1]
        local = jumps[max(0, i - block // 2 - 1):min(len(jumps), i + block // 2)]
        local_median = sorted(local)[len(local) // 2] if local else 0.0
        if delta > floor and delta > max(0.002, local_median * 20):
            suspects.append({"sample": i, "jump": delta})
    return {"max_ratio": max((item["jump"] / max(baseline, 1e-8) for item in suspects), default=0.0),
            "suspect_positions": suspects[:20]}


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
        "render_envelope": [envelope_metrics(ch, out_rate) for ch in out],
        "render_attacks": [transient_metrics(ch, out_rate) for ch in out],
        "boundary_anomalies": [boundary_anomaly(ch, boundary) for ch in out],
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
        assert "render_envelope" in result and "boundary_anomalies" in result
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
