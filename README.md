# Banded Grains

Multiband granular audio effect for Windows x64, implemented in C++17 with JUCE 8.0.12. The build produces a VST3 plugin with matching mono or stereo input/output buses. MIDI input and output are disabled.

![Banded Grains interface](docs/banded-grains.png)

## Signal path

```text
Audio input -> crossover bank -> band-to-module routing -> granular modules -> summed output
                    |
                    +-> raw band audition
```

The crossover bank supports 1-12 bands using up to 11 fourth-order Linkwitz-Riley crossovers. All-pass compensation gives the recombined dry bands a flat magnitude response; the filters still introduce phase shift. Cutoff changes are smoothed over 30 ms. Changing the band count resets filter state and can produce a transient.

Each band routes to one of 12 independent processing modules. Bands assigned to the same module are summed before granular processing. All modules continue processing after routing changes, allowing existing grain and feedback tails to decay. The initial configuration routes one full-range band to Module 1.

Each module contains:

- A four-second circular recording buffer per channel.
- A fixed pool of 32 grains with shared stereo timing and independently adjustable attack/release cosine fades.
- Linear-interpolated playback with pitch transposition, reverse playback, and stereo balance randomization.
- A feedback path taken before the output dry/wet mix, with an interpolated delay, saturation, and low-pass damping. The return is added to the recording buffer.
- Smoothed dry/wet mixing and overlap gain compensation.

Pitch transposition changes the read rate within a grain while its output duration remains fixed. The wet path has capture lookback latency and an initial buffer-fill period. Overlap compensation controls average gain; the output has no peak limiter.

## Module parameters

Ranges below are the exposed plugin parameter ranges. Attack and release are shown as fractions of grain duration; their host parameters (`shape` and `release`) range from 0-100 and map inversely from 50% to 2.5%.

| Parameter | Range | Default | Behavior |
| --- | --- | --- | --- |
| Grain size | 20-250 ms | 100 ms | Output duration of each grain |
| Density | 2-40 grains/s | 15 grains/s | Grain launch rate |
| Dry / Wet | 0-100% | 50% | Module output mix |
| Attack / Release | 2.5-50% each | 50% each | Independent cosine fade durations |
| Feedback | 0-95% | 0% | Feedback return gain |
| Repeat time | 10-2000 ms | 350 ms | Delay within the feedback loop |
| Damping | 100-20000 Hz | 6000 Hz | Feedback low-pass cutoff, internally capped at 0.45 times sample rate |
| Position | 0-1500 ms | 150 ms | Additional lookback behind the grain safety offset |
| Spray | 0-1000 ms | 200 ms | Total randomized position width centered on Position |
| Base pitch | -24 to +24 semitones | 0 | Added to the selected pitch offset |
| Reverse chance | 0-100% | 0% | Probability of reverse playback at grain launch |
| Stereo spread | 0-100% | 0% | Random per-grain stereo balance; inactive for mono |
| Freeze | Off / On | Off | Stops recording and holds the recording head while grains continue |

Each module also has four integer pitch offsets (-24 to +24 semitones) with independent probabilities (0-100%). The default offsets are -12, -7, +7, and +12 semitones, with all probabilities initially zero. Unassigned probability selects a zero offset. If the sum exceeds 100%, the probabilities are normalized proportionally. Base pitch is added afterward, and the combined transposition is clamped to -36 to +36 semitones.

Capture positions are clamped to the valid recording-buffer region. Grain settings are captured at launch; changes affect subsequent grains. Feedback, repeat time, damping, dry/wet mix, and overlap gain are smoothed. Changing repeat time can bend the pitch of the feedback return. The audible loop interval also includes granular capture/playback delay.

Freeze preserves the module's recorded audio while its dry path remains live. A frozen empty buffer produces no wet signal.

## Modulation

Up to four shared LFOs are available, each with up to four connections across the 12 modules. All 22 module parameter targets are available, including pitch probabilities and freeze.

| Setting | Values |
| --- | --- |
| Waveform | Sine, triangle, ramp up, ramp down, square, random hold |
| Free-running rate | 0.02-20 Hz |
| Tempo cycle length | 1/16 note, 1/8 note, 1/4 note, 1/2 note, 4 beats, 8 beats, 16 beats |
| Connection data | Destination module, target parameter, minimum, maximum |

Modulation is evaluated once per processing chunk of at most 256 samples. Each connection maps the LFO output into the destination's normalized parameter range; the editor displays endpoints in destination units. Reversed endpoints invert the mapping. If multiple connections address the same target, the last connection in source/connection order wins. Modulation overrides the effective DSP value without writing back to the base host parameter.

Free-running LFOs advance continuously. Tempo-based LFOs pause when the host reports stopped transport and use 120 BPM if tempo is unavailable. Phase resets during audio preparation; synchronization follows tempo rather than host timeline position.

To create a connection, drag an LFO output socket onto a visible control input. Hovering a module tab during a drag changes the destination module. Hover a socket to display its cables; click a connected input to select the corresponding connection.

## Routing, monitoring, and state

- Double-click the spectrum to insert a crossover, drag a divider to change its frequency, and right-click a divider to remove it. Removal retains the lower band's routing.
- Band dropdowns select destination modules. The routing row scrolls horizontally when needed.
- A band's **s** button auditions its crossover output before granular processing. A module's **s** button solos its processed output, including tails. These modes are mutually exclusive and use crossfades.
- Audition and solo are temporary. They are cleared on band-count changes and state recall, and are excluded from saved state.
- Parameters, crossovers, routing, and modulation connections are serialized through `AudioProcessorValueTreeState`. Module 1 uses unprefixed parameter IDs; Modules 2-12 use `m2_` through `m12_` prefixes.
- Recorded audio is excluded from serialized state. Freeze is reset to off during state recall.

The spectrum reads input audio before granular processing. Its FFT runs on the GUI thread. Spectrum samples and grain-display snapshots pass from the audio thread through bounded FIFOs. The grain display represents the selected module's recording head and active read heads.

## Build

### Requirements

- Windows x64.
- Visual Studio 2022 C++ build tools and a Windows SDK.
- CMake 3.22 or newer, either on `PATH` or bundled with Visual Studio.
- Git and network access for the initial JUCE checkout.
- PowerShell.

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The script clones JUCE tag `8.0.12` into `JUCE` if that directory lacks `CMakeLists.txt`, configures the Visual Studio 2022 x64 generator, builds Release targets, and runs both CTest suites. An existing JUCE checkout is reused without a version check.

The generated plugin bundle is:

```text
build/Granular_artefacts/Release/VST3/Banded Grains.vst3/
```

`JUCE/`, `build/`, and `dist/` are excluded from Git. The build script does not package a ZIP archive or install the plugin.

### Installation

Copy the entire `.vst3` bundle directory to a host-scanned VST3 location. The standard system directory is:

```text
C:\Program Files\Common Files\VST3\
```

Alternatively, configure the host to scan a custom directory. Close active plugin instances before replacing a bundle, then rescan in the host. The plugin is listed as **Banded Grains**, with manufacturer **Granular**.

## Tests

`build.ps1` runs the following suites after building:

| CTest target | Coverage |
| --- | --- |
| `GranularEngineTests` | Dry passthrough, wet output, pitch selection, feedback timing and damping, playback controls, freeze, buffer wraparound, stereo alignment, finite output under parameter changes, and reinitialization |
| `GranularPluginCheck` | VST3 discovery/loading, editor interactions, crossover reconstruction and isolation, frequency-specific routing, state recall, mono/stereo layouts, variable block sizes, modulation, reordered crossover dragging, and audition reset after automated band-count changes |

Engine and crossover checks include 44.1, 48, and 96 kHz. Plugin layout checks exercise block sizes of 1, 64, 257, and 1024 samples. The audio processor subdivides host buffers into chunks of at most 256 samples using buffers allocated during preparation.

For an editor PNG, run the plugin-check executable with an optional output path after the VST3 path:

```powershell
& '.\build\GranularPluginCheck_artefacts\Release\GranularPluginCheck.exe' `
    '.\build\Granular_artefacts\Release\VST3\Banded Grains.vst3' `
    '.\build\editor-preview.png'
```

This also writes `playback-preview.png` beside the requested image.

## Source layout

| Path | Responsibility |
| --- | --- |
| `Source/GranularEngine.h` | JUCE-independent grain engine, recording buffers, envelopes, pitch, and feedback |
| `Source/BandSplitter.h` | Linkwitz-Riley crossover bank, phase compensation, and cutoff smoothing |
| `Source/PluginProcessor.cpp` | Host integration, routing, module processing, modulation, parameters, and state |
| `Source/PluginEditor.cpp` | Spectrum, routing controls, grain visualization, envelope editing, and cable interaction |
| `Tests/GranularEngineTests.cpp` | DSP engine tests |
| `Tests/PluginCheck.cpp` | Plugin integration and editor tests |
| `CMakeLists.txt` | Plugin and test targets |
| `build.ps1` | Windows dependency checkout, build, and test entry point |

## Dependency licensing

JUCE is a third-party dependency with separate licensing terms. Its license is available at `JUCE/LICENSE.md` after checkout.
