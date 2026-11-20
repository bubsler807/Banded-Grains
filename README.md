# Banded Grains

Banded Grains is a multiband granular audio effect for Windows x64. It splits incoming audio into frequency bands and sends them to independent granular processors, so different parts of a sound can have their own grain timing, pitch, feedback, and modulation.

The plugin runs as a VST3 effect in a DAW and supports mono and stereo audio. It is built with C++17 and JUCE 8.0.12.

![Banded Grains interface](docs/banded-grains.png)

## How it works

Create up to 12 frequency bands and route each one to any of 12 processing modules. Multiple bands can share a module; their signals are combined before processing. The default setup sends the full frequency range to Module 1.

Each module records incoming audio into a four-second buffer and plays back short overlapping fragments called grains. Grain duration, playback position, pitch, direction, and envelope determine the result. Each module also has its own feedback loop and dry/wet mix.

The frequency bands use 24 dB/octave Linkwitz-Riley crossovers with phase compensation. Their transitions overlap, and the recombined dry signal has a flat frequency response. Moving crossover frequencies is smoothed; adding or removing bands can cause a brief transient.

## Grain and feedback controls

Select a module tab to edit its settings. The **Grains** panel contains timing, envelope, and feedback controls.

| Control | Range | Function |
| --- | --- | --- |
| Grain size | 20-250 ms | Duration of each grain |
| Density | 2-40 grains/s | Number of grains launched per second |
| Dry / Wet | 0-100% | Balance between the module's input and granular output |
| Attack / Release | 2.5-50% of grain duration each | Independent fade-in and fade-out, adjusted by dragging the envelope slopes |
| Feedback | 0-95% | Amount of processed audio returned to the recording buffer |
| Repeat time | 10-2000 ms | Delay within the feedback loop |
| Damping | 100-20000 Hz | Low-pass filtering in the feedback loop |

Each module has four pitch choices from -24 to +24 semitones, each with its own probability. Unassigned probability plays without an additional pitch offset. If the probabilities add up to more than 100%, they are scaled proportionally.

Feedback is taken before the dry/wet mix, so changing the mix does not change the feedback amount. The loop includes saturation and damping to control buildup. Repeat time sets the delay within this loop; grain capture and playback add to the audible repeat interval. Changing repeat time smoothly can bend the pitch of the return.

## Playback controls

The **Playback** panel controls which part of the buffer is used and how each grain plays it.

| Control | Range | Function |
| --- | --- | --- |
| Position | 0-1500 ms | Additional lookback into previously recorded audio |
| Spray | 0-1000 ms | Width of the random position variation around Position |
| Base pitch | -24 to +24 semitones | Transposition applied alongside the four pitch choices |
| Reverse chance | 0-100% | Probability that a new grain plays backward |
| Stereo spread | 0-100% | Random stereo balance for each grain; inactive in mono |
| Freeze | Off / On | Holds the recorded buffer while grains continue playing |

Combined pitch is limited to -36 to +36 semitones. Pitch shifting changes playback speed within each grain while its duration stays fixed. Grain settings take effect on newly launched grains.

The grain display shows the selected module's recording head and active grains. Freeze stops the recording head; the dry signal remains live. Recorded audio is not saved with a DAW session, and freeze resets to off when settings are recalled.

## Routing and monitoring

- **Add a band:** double-click inside the spectrum.
- **Adjust a split:** drag its divider horizontally.
- **Remove a split:** right-click its divider. The lower band's routing is retained.
- **Route a band:** choose a destination in its module dropdown. Scroll the routing row to reach additional bands.
- **Audition a band:** click its **s** button below the spectrum to hear that frequency range before granular processing.
- **Solo a module:** click **s** in its module tab to hear its processed output, including grain and feedback tails.

Band audition and module solo replace one another. Click the active button again to restore the full mix. These monitoring states are temporary and clear when the band count changes or settings are recalled.

The spectrum displays the incoming audio before granular processing. Modules continue running after routing changes so existing tails can decay.

## Modulation

Up to four shared LFOs can modulate controls across all modules. Each LFO supports four connections, with a separate destination and minimum/maximum range for each connection.

Available waveforms are sine, triangle, ramp up, ramp down, square, and random hold. LFOs run at 0.02-20 Hz or follow tempo-based cycles from a sixteenth note to 16 beats.

To connect an LFO, drag its output socket onto a control's input socket. Hover a module tab while dragging to switch modules. Hover a socket to display its cables, or click a connected input to select its connection. Connection ranges can be typed in the destination control's units; reversing the endpoints reverses the modulation.

Modulation acts on the processing value without changing the underlying knob or DAW automation value. If several connections target the same control, the last connection in source/connection order takes priority.

Tempo-based LFOs pause when the host reports stopped transport and use 120 BPM if host tempo is unavailable. They follow the tempo without locking their phase to the DAW timeline.

## Processing notes

- The wet signal has an initial fill period and an audible delay from reading previously recorded audio. The dry path remains immediate within each module.
- Low density or short grains can produce gaps. Overlap compensation controls average level, but the output has no peak limiter.
- Parameters, crossover positions, routing, and modulation connections are saved with the DAW session. Recorded buffer audio and temporary solo/audition states are not.
- Audio buffers are allocated during preparation. Spectrum analysis and drawing run on the interface thread.

## Build from source

Requirements:

- Windows x64 and PowerShell.
- Visual Studio 2022 C++ build tools with a Windows SDK.
- CMake 3.22 or newer, on `PATH` or bundled with Visual Studio.
- Git and network access for the initial JUCE download.

Run from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The script downloads JUCE tag `8.0.12` if it is missing, builds the Release plugin, and runs the tests. An existing JUCE checkout is reused.

Output:

```text
build/Granular_artefacts/Release/VST3/Banded Grains.vst3/
```

Build outputs and the downloaded JUCE source are excluded from the repository. The script does not install the plugin or create a ZIP package.

## Installation

Copy the entire `Banded Grains.vst3` bundle folder to a VST3 location scanned by your DAW. The standard Windows directory is:

```text
C:\Program Files\Common Files\VST3\
```

You can also use a custom scan directory if your DAW supports it. Close active plugin instances before replacing the bundle, then rescan in your DAW. Look for **Banded Grains** under manufacturer **Granular**.

## Development

| File | Purpose |
| --- | --- |
| `Source/GranularEngine.h` | Grain playback, recording buffers, envelopes, pitch, and feedback |
| `Source/BandSplitter.h` | Crossovers and phase compensation |
| `Source/PluginProcessor.cpp` | Audio routing, modules, modulation, parameters, and saved state |
| `Source/PluginEditor.cpp` | Spectrum, controls, grain display, and patch cables |
| `Tests/GranularEngineTests.cpp` | DSP engine tests |
| `Tests/PluginCheck.cpp` | VST3 loading, routing, state recall, modulation, and interface tests |

The build runs both test suites automatically. Tests cover engine and crossover behavior at 44.1, 48, and 96 kHz, along with mono/stereo operation and variable host block sizes.

JUCE has separate licensing terms. See `JUCE/LICENSE.md` after downloading the dependency.
