# MixPilot Studio 9.0.1 — Audio Intelligence Foundation

This is a full source project for the existing MixPilot Studio VST3, based on the modular 8.9.6 codebase with the first *independent* Audio Intelligence analysis engine added.

## What's included

- Existing modular effects, DSP and plugin UI from MixPilot Studio 8.9.6.
- New `Source/Analysis/AudioIntelligenceEngine.h` and `.cpp`.
- New `tests/audio_intelligence_test.cpp`.
- Updated `CMakeLists.txt` and `.github/workflows/windows-vst3.yml`.
- Existing standalone DSP and source regression tests.

### IMPORTANT scope

The new Audio Intelligence engine is compiled and exercised by an independent automated test; it is **not yet connected** to the live ANALYZE button, Qwen, or the signal-processing chain.

**No automatic iterative feedback** is implemented in 9.0.1. The new engine reports measurements and advisory corrections only; it does not auto-apply effects, evaluate its own output, or run feedback loops. This explicit limitation is also checked by the legacy source regression test. Audio quality and Windows VST3 compilation are unverified until GitHub Actions and a Mixcraft test complete.

## Upload to GitHub

The archive's **root** contains `CMakeLists.txt`, `Source/`, `tests/`, `.github/`, and this README. Upload these files and folders as a group at your repository root, retaining their relative paths. Do not create an extra folder layer.

If updating a repository that currently has a nested project, either move this complete project to the root or replace the existing project files within their existing project directory, and place `.github/workflows/windows-vst3.yml` at the *repository* root. Make sure only the intended `CMakeLists.txt` is discovered by the workflow.

GitHub Actions workflow: **Build MixPilot Studio 9.0.1 - Audio Intelligence Windows VST3**.

Keep a backup of your existing installed MixPilot Studio VST3: the plugin identity remains unchanged.

## Testing

Local standalone tests passed under GCC; the Audio Intelligence test also passed with Clang. Full JUCE/Windows/MSVC compilation and Mixcraft behavior were not tested locally.
