#!/usr/bin/env python3
"""Generate deterministic mono/stereo PCM fixture WAVs for Riche CI."""
import math
import struct
import sys
import wave
from pathlib import Path

directory = Path(sys.argv[1] if len(sys.argv) > 1 else "build-fast/riche-fixtures")
directory.mkdir(parents=True, exist_ok=True)
rate = 48000
duration = 2
for kind in ("drums", "voice", "guitar"):
    path = directory / (kind + ".wav")
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        samples = bytearray()
        for i in range(rate * duration):
            t = i / rate
            if kind == "drums":
                phase = t % 0.25
                value = (0.75 * math.exp(-phase * 75) *
                         math.sin(2 * math.pi * (95 * phase + 130 * phase * phase)))
            elif kind == "voice":
                value = 0.25 * (math.sin(2 * math.pi * 170 * t) +
                                0.35 * math.sin(2 * math.pi * 340 * t)) * (0.5 + 0.5 * math.sin(2 * math.pi * 3 * t))
            else:
                phase = t % 0.4
                value = 0.45 * math.exp(-phase * 7) * (
                    math.sin(2 * math.pi * 220 * t) +
                    0.4 * math.sin(2 * math.pi * 440 * t))
            left = max(-32768, min(32767, round(value * 32767)))
            right = round(left * 0.85)
            samples.extend(struct.pack("<hh", left, right))
        wav.writeframes(samples)
    print(path)
