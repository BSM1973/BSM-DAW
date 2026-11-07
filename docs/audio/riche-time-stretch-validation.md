# Liberty — Riche Time Stretch audio validation

Status: **not yet acoustically validated**. A successful build is not an audio quality result.

## Test setup
- Export unprocessed references as 48 kHz / 24-bit stereo WAV; disable effects, normalization and master limiting.
- Import each reference in Liberty, select **Riche** Warp and render or loop back the output at the same sample rate.
- Preserve input/output WAVs, OS, device, buffer size, commit SHA and any Warp markers.
- Compare against a direct/no-stretch baseline; repeat at 64, 256 and 1024 sample audio buffers.

## Matrix
| Source | Ratios (output duration / original duration) | Focus |
|---|---|---|
| Dry drums (kick/snare/hi-hat) | 0.5x, 0.75x, 1.5x, 2x | transient onset, clicks, flams, attack smear |
| Dry spoken and sung voice | 0.75x, 1.5x, 2x | metallic texture, syllable doubling, pumping |
| Acoustic guitar (picked and strummed) | 0.75x, 1.5x | pick attack, sustain, stereo stability |
| Sine sweep, impulses, silence | 0.5x, 1.5x | discontinuities, DC offsets, unexpected output |

## Objective checks
1. Scan rendered WAV for NaN/Inf and clipping (absolute amplitude >= 1.0); report peak and RMS, do not normalize results away.
2. Detect discontinuities via absolute adjacent-sample differences; compare against the no-stretch reference and inspect flagged waveform positions manually (legitimate drum attacks can create large differences).
3. At each buffer size, examine transitions around 64/256/1024-sample boundaries and repeat with the same input. Buffer-size-dependent clicks are a failure.
4. Verify stereo left/right timing and relative level are stable; test mono sources too.
5. Test start, stop, seek, loop wrap, switching Warp mode, and replacing the audio source. Listen for stale envelope state and first-block clicks.
6. Check CPU load and xruns during real-time playback. Compare against the previous green baseline.

## Acceptance
- No NaN/Inf, unintended silence, reproducible boundary clicks, or audio-thread dropouts.
- No audible flamming or unacceptable smearing on the dry-drum attacks; no objectionable voice doubling or guitar modulation.
- Changes must not regress the other Warp modes or existing transport functions.
- Record actual measurements and listening notes before marking the mode acoustically validated.

## Notes
The Riche transient envelope currently persists across contiguous callbacks per track. Its continuity is reset on discontinuity or content revision; listen specifically at loop wraps and after source replacement. The existing diagnostic build checks compilation and other regressions, not this listening matrix.
