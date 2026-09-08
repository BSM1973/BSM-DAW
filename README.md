# BSM DAW

A professional digital audio workstation developed by BSM (Bijou Studio Music).

## Vision

BSM DAW is designed as a modern, musician-focused DAW with a robust real-time audio engine, MIDI production, mixing, plugin hosting, automation, and a distinctive BSM workflow.

## Development status

**Version:** 0.1.0 — Project foundation

The project is currently establishing its architecture and build system before implementing the real-time audio engine.

## Planned technology

- C++
- JUCE
- CMake
- GitHub Actions / continuous integration
- macOS first, with Windows support planned

## Initial roadmap

- [ ] Project foundation
- [ ] Build system
- [ ] Main application window
- [ ] Audio device management
- [ ] Real-time audio engine
- [ ] Transport
- [ ] Audio tracks
- [ ] MIDI engine and piano roll
- [ ] Mixer and routing
- [ ] Plugin hosting (VST3 / AU / CLAP)
- [ ] Automation
- [ ] Project format
- [ ] Audio editing

## Philosophy

Every milestone must remain buildable and testable. The audio thread is treated as a real-time critical path, with strict separation between the audio engine, project model, plugin layer, and user interface.

© BSM / Bijou Studio Music
