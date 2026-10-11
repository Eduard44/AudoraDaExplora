# ClearVoice

A personal voice-cleaning plugin: three knobs for cleaning (Noise, Reverb, Volume) and a
separate, switchable Voice section that makes the voice deeper or higher without sounding unnatural.

## Controls

| Control | What it does |
| --- | --- |
| **Noise** | How much background noise is removed (neural noise suppression, RNNoise). 0 % = off. |
| **Reverb** | How much room echo is removed (spectral late-reverb suppression). 0 % = off. |
| **Volume** | Output level, -24 to +12 dB. |
| **Voice switch** | Turns the whole Voice section on or off. Off = your voice is untouched by it. |
| **Pitch knob** | Centre = natural voice with a calmer finish. Left = deeper, right = higher (up to about 5 semitones each way). Double-click to return to centre. |

"Calm" (when the Voice switch is on) means a gentle de-esser to soften harsh S sounds, a mild
2:1 leveller to steady loud peaks, and a very slight softening of the top end. The pitch shifter
moves the pitch and moves the voice's "shape" (formants) only about half as far, which is what
keeps low settings from sounding like a monster and high settings from sounding like a chipmunk.

## Getting the plugin without installing a compiler (GitHub Actions)

1. Create a free account at github.com and a new **private** repository.
2. Upload this whole folder (drag and drop works in the GitHub web page, or use `git push`).
3. Open the repository's **Actions** tab. The build starts by itself (about 10 minutes the first time).
   If it doesn't, pick "Build ClearVoice (Windows)" and press **Run workflow**.
4. When it finishes, open the run and download the **ClearVoice-Windows** artifact. Unzip it.
5. Copy the `ClearVoice.vst3` folder to `C:\Program Files\Common Files\VST3\`.

## Using it in your software

- **DaVinci Resolve (Fairlight page):** put the plugin in the VST3 folder above, then in Resolve open
  *Preferences > Audio Plugins* and press *Refresh*, then add it from the track's Effects list.
  Resolve may need a restart, and you may need to enable it in the plugin list.
- **Audacity:** supports VST3. Use *Effect > Add / Remove Plug-ins...*, find ClearVoice and enable it.
  Audacity applies effects to selected audio rather than live.
- **OBS Studio:** OBS's built-in VST filter only supports **VST 2.x**, so it will not list this
  VST3. Two ways around that:
  1. Run `ClearVoice-Standalone.exe`, choose your microphone as input and a virtual audio cable
     (for example VB-Cable) as output, then add that cable as an audio capture source in OBS.
  2. Use a free VST3 host in between (such as Reaper's free trial or a dedicated mic-processing
     app) and send its output to OBS.
  Building a VST2 version is not included: Steinberg no longer licenses the VST2 SDK.

## Latency

The plugin reports its delay to the host so editors like Resolve line things up automatically
(about 85 ms at 48 kHz: noise stage ~21 ms, reverb stage ~21 ms, voice stage ~43 ms). The delay is
constant, even when the Voice switch is off. For live monitoring through the standalone app this
is noticeable; for recording/editing it is not.

## Building locally (optional)

Requires CMake 3.22+ and Visual Studio 2022 (Desktop C++ workload):

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The plugin appears under `build/ClearVoice_artefacts/Release/`.

## Project layout

- `Source/NoiseReducer.h`: RNNoise wrapper (resamples to 48 kHz when the host runs at another rate)
- `Source/DeReverb.h`: spectral late-reverb suppression
- `Source/VoiceShifter.h`: formant-preserving pitch shifter and the calm stage
- `Source/PluginProcessor.*`, `Source/PluginEditor.*`: plugin glue and the knob interface
- `ThirdParty/rnnoise`: RNNoise 0.1.x (BSD-3-Clause, see `COPYING`). One line in `celt_lpc.c` was
  changed so it compiles with Visual Studio.
- JUCE 8 is downloaded automatically at build time. JUCE 8 is dual-licensed (AGPLv3 or a
  commercial license); building it for your own personal use is fine.

This is an original implementation. It is not affiliated with, and contains no code from,
Antinode Audio's Clear.
