"""Validate native diagnostic WAVs independently of the C++ writer."""
import math
import struct
import sys
import wave
from pathlib import Path


def metrics(path, expected_frames):
    with wave.open(str(path), "rb") as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 48000), path
        assert wav.getnframes() == expected_frames, path
        raw = wav.readframes(expected_frames)
    assert len(raw) == expected_frames * 2, path
    data = [x[0] / 32768 for x in struct.iter_unpack("<h", raw)]
    rms = math.sqrt(math.fsum(x * x for x in data) / len(data))
    peak = max(map(abs, data))
    assert 0.01 < rms < 0.9 and peak < 0.98, (path, rms, peak)
    print(f"{path.name}: frames={len(data)}, rms={rms:.8f}, peak={peak:.8f}")
    return rms


root = Path(sys.argv[1])
dry = metrics(root / "warp_dry.wav", 48000)
wet = metrics(root / "warp_x4_rms_matched.wav", 192000)
delta = 20 * math.log10(wet / dry)
assert abs(delta) <= 0.02, delta
print(f"Native WAV A/B RMS delta: {delta:.6f} dB")
