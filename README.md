<img src="resources/icon.png" alt="" width="112" align="right">

# NDI Audio Bridge

Send the inputs of one or more audio interfaces (e.g. a TASCAM US-4x4HR plus a Scarlett) to
vMix as separate NDI audio sources, bring an NDI return feed back from vMix, and build
per-announcer headphone mixes on any interface's outputs by drawing wires. Includes a virtual
audio output so the computer's own sound can go through it too.

Runs on **macOS** (11+, Apple Silicon and Intel) and **Windows** (10/11, x64).

![NDI Audio Bridge](docs/screenshot.png)

```
 Interface inputs ─┬────────────► NDI sources "Announcer 1", "Guest Mic", ... ──► vMix
 (up to 4 devices) │
 Computer audio ───┼────────────► NDI source  "Computer Audio" ─────────────────► vMix
 (virtual output)  │
                   ▼
 vMix NDI out ──► WIRES + MIXER ──► any interface's outputs: stereo pairs or
                  (level per source        separate mono headphone feeds
                   per mix)
```

## What it does

| Where | What it's for |
|---|---|
| **Routing: wires** | Sources on the left (grouped by interface, then computer audio and the vMix return), headphone mixes on the right. **Drag from a source's dot onto a mix** to add it. Click a mix to edit it below. |
| **Routing: mixer** | One tab per headphone mix. Each source gets a strip: **ON**, **L / R** (for stereo mixes, so a mic can go in one ear only), a fader for its level in this mix, and a meter. The master strip names the mix and sets its overall level. **STEREO / 2× MONO** turns an output pair into one stereo mix or two separate mono feeds. |
| **Source pop-out** | Click any source. Rename it (that's its NDI name in vMix), switch its NDI send on/off, stereo/mono send, **cough mute** (silences the send to vMix, not the headphones), trim, and every mix it feeds with its own level. For **Computer Audio** pick the virtual device; for **vMix Return** pick the NDI source and its buffer. |
| **Interfaces** | Add up to four audio interfaces, each with its own driver, sample rate and buffer. **Inputs used / outputs used** hides channels you don't need. |
| **Companion** | Cough mute from **Bitfocus Companion** (Stream Deck), with an on-air light on the button. Lists the OSC path for every mic, with copy buttons. See [Bitfocus Companion](#bitfocus-companion). |
| **Settings** | Light or dark mode, the buffers, NDI runtime info. |
| **Sidebar** | Hover it to expand. At the bottom: CPU (this app vs. the whole computer, and how much is free) and network (what NDI is sending/receiving and how much of the link is free). |

Everything you change is saved automatically and restored the next time you open the app.

> **TASCAM tip:** turn the interface's **MONITOR MIX** knob fully to **COMPUTER**. Otherwise the
> hardware also sends every input straight to the headphones and the app can't turn them off.

---

## Setup: macOS

1. **Install the NDI Runtime** (free): <https://ndi.link/NDIRedistV6Apple>. It installs
   `libndi.dylib`, which the app loads. NDI Tools (<https://ndi.video/tools/>) is worth having too
   (NDI Access Manager, Studio Monitor), but the app needs the runtime itself.
2. **Install the app.** Unzip `NDI-Audio-Bridge-macOS.zip`. These builds aren't notarized by
   Apple, so clear the download flag on the whole folder once in Terminal (adjust the path if you
   unzipped somewhere else):
   ```bash
   xattr -dr com.apple.quarantine ~/Downloads/"NDI Audio Bridge"
   ```
   Then drag **NDI Audio Bridge.app** to Applications.
3. **Install the virtual output.** In the unzipped folder, double-click
   **Install Virtual Audio Device.command** and enter your password when asked. This adds a
   sound device called **NDI Audio Bridge**.
4. **System Settings > Sound > Output**: choose **NDI Audio Bridge**. Everything the Mac
   plays now goes into the app.
5. Open **NDI Audio Bridge** and allow **Microphone** (needed to read the interface inputs) and
   **Local Network** (needed for NDI) when macOS asks.
6. The app picks the TASCAM automatically on first launch if it's plugged in. To check or add
   another interface, hover the sidebar and open **Interfaces**: 48000 Hz, buffer **128**
   (raise it to 256 if you hear crackles).
7. On **Routing**, click **Computer Audio** and under *Plays from* choose **NDI Audio Bridge**.

To remove the virtual device later, run the installer with `--uninstall` in Terminal:

```bash
bash ~/Downloads/"NDI Audio Bridge"/"Install Virtual Audio Device.command" --uninstall
```

If you'd rather use stock BlackHole, `brew install blackhole-2ch` and pick
**BlackHole 2ch** in steps 4 and 7 instead.

## Setup: Windows

1. **Install NDI Tools** (free) from <https://ndi.video/tools/>. It includes the NDI runtime
   (or install just the runtime: <https://ndi.link/NDIRedistV6>).
2. **Install the TASCAM driver** (it includes the ASIO driver).
3. **Install VB-Audio Virtual Cable** (free) from <https://vb-audio.com/Cable/>. That's
   Windows' equivalent of the virtual output. Then right-click the speaker icon > **Sound
   settings** and set the output device to **CABLE Input**.
4. Unzip `NDI-Audio-Bridge-Windows.zip` and run **NDI Audio Bridge.exe**. When Windows Firewall
   asks, allow it on **Private networks** (NDI won't work otherwise).
5. **Interfaces** (in the sidebar): driver **ASIO**, the TASCAM, 48000 Hz, buffer 128. Use ASIO:
   in "Windows Audio" mode the TASCAM's inputs may appear as separate stereo devices.
6. On **Routing**, click **Computer Audio** and under *Plays from* choose **CABLE Output**.

## Setup: vMix

**Getting the announcers into vMix:** *Add Input > NDI / Desktop Capture* and pick
`YOUR-MAC (Announcer 1)`, `YOUR-MAC (Announcer 2)`, and so on. Each one is a separate vMix input
with its own fader. Add `YOUR-MAC (Computer Audio)` the same way if you send it.

**Sending a return feed back:** in vMix, enable NDI on one of the outputs (*Settings > Outputs /
NDI / SRT*) and set that output's audio to the mix the announcers should hear. Then click
**vMix Return** in the app and pick it under *NDI source* (names look like `VMIX-PC (vMix - Output 2)`).

> **Use a mix-minus for the return.** If the return feed contains the announcers' own mics,
> they'll hear themselves twice: once instantly through the app, then again delayed through
> vMix. Point the return at a vMix bus (e.g. Bus A) that carries program audio *without* the
> announcer mics, and let the headphone mixes handle hearing themselves and each other.

## Bitfocus Companion

The app listens for OSC on **UDP port 9500**, so Companion buttons can cough-mute a mic and
light up only while that mic is actually reaching vMix. The **Companion** page in the app shows
this computer's IP, the last command received, and the paths for every mic, with copy buttons.

**1. Add the connection.** In Companion, add **Generic: OSC**:

| Field | Value |
|---|---|
| Target Hostname or IP | the IP of the computer running NDI Audio Bridge (`127.0.0.1` if it's the same computer) |
| Target Port | `9500` |
| Protocol | UDP |
| Listen for Feedback | ticked |
| Source Port | any free port, e.g. `9501` |

The app sends feedback to whatever address a command comes from, and remembers the first one
(in *Send feedback to* on the Companion page), so the buttons are right again after a restart.

**2. A cough button.** Press action **Send message without arguments**, OSC Path
`/bridge/in/1/1/cough/toggle` (interface 1, input 1). For press-to-cough instead, use
`…/cough/on` as the press action and `…/cough/off` as the release action.

**3. The on-air light.** Feedback **Listen for OSC messages (Integer)**, OSC Path
`/bridge/in/1/1/live`, Value `1`, with a red background. A second feedback on
`/bridge/in/1/1/cough` = `1` can show the button differently (e.g. grey "COUGH") while muted.

| Path | Direction | Meaning |
|---|---|---|
| `/bridge/in/<i>/<n>/cough/on` · `/off` · `/toggle` | to the app | Cough mute on, off, toggle. `<i>` = interface number, `<n>` = input number (the Companion page lists the exact path for every mic) |
| `/bridge/in/<i>/<n>/cough` + integer | to the app | 1 = mute, 0 = unmute (without an argument it toggles) |
| `/bridge/computer/cough/…` | to the app | The same for computer audio |
| `/bridge/refresh` | to the app | Send every value again |
| `/bridge/in/<i>/<n>/live` | from the app | **1** = going to vMix right now: NDI send on, cough off, interface running and vMix connected to it |
| `/bridge/in/<i>/<n>/cough` | from the app | 1 = cough mute on |
| `/bridge/in/<i>/<n>/receivers` | from the app | How many NDI receivers (e.g. vMix) have this source open |
| `/bridge/in/<i>/<n>/name` | from the app | The mic's name (for button text via the *OSC message value* feedback) |

Values are sent when they change and all of them every 5 seconds. The port can be changed (or
OSC control turned off) on the Companion page.

---

## Latency

* Interface buffer 128 samples @ 48 kHz ≈ 2.7 ms. Headphone mixes run at this latency,
  so announcers hear themselves and each other almost instantly.
* A mix that plays an input from a *different* interface adds the **between interfaces** buffer
  (default 25 ms, in Settings), because the two interfaces run on separate clocks.
* NDI sends go out in 10 ms frames; vMix adds its own buffering on top.
* The return **buffer** (default 60 ms, in the vMix Return pop-out) covers network jitter. On a
  solid wired network you can lower it to ~30 ms. If its status line shows the *dropouts* count
  going up, raise it. The status line also shows the live buffer and the drift correction in ppm.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Red "NDI runtime not found" | Install the NDI Runtime (links in the setup steps above), restart the app. To point at a specific library, set `NDI_AUDIO_BRIDGE_NDI_LIB=/path/to/libndi.dylib`. |
| vMix doesn't list the sources | Same network/subnet? Windows Firewall allowed on Private networks? On macOS, Local Network permission (System Settings > Privacy & Security > Local Network). Across subnets, add the vMix PC's IP with **NDI Access Manager** (part of NDI Tools) on both machines. |
| Status says "Live, 0 receivers" | The source is on the network, but vMix hasn't added it as an input yet. |
| Meters move but no sound in vMix | Check the source isn't on **cough mute** and the input is unmuted in vMix. |
| Everyone hears every mic whatever the mixes say | Turn the TASCAM's MONITOR MIX knob fully to COMPUTER. |
| An interface chip is red | It's unplugged or busy. The app retries every few seconds; check **Interfaces**. |
| No input level at all on macOS | System Settings > Privacy & Security > Microphone: enable NDI Audio Bridge. |
| Computer audio silent | System (or Windows) output must be set to the virtual device, and the same device picked under *Plays from* in the **Computer Audio** pop-out. |
| Return crackles or drops out | Raise the return **Buffer**, use wired Ethernet rather than Wi-Fi. |
| Companion buttons do nothing | On the **Companion** page, check *Last command*. Nothing there: wrong IP/port in Companion, or a firewall: allow NDI Audio Bridge on Private networks (Windows) or accept incoming connections (macOS). |
| Companion buttons work but never light up | Tick **Listen for Feedback** and set a **Source Port** in the Companion connection, then press a button once. *Send feedback to* on the Companion page should show Companion's IP and that port. |
| `live` stays 0 although the mic isn't coughed | `live` also needs vMix to have the source open (*receivers* ≥ 1) and the NDI send switched on. |

---

## Building from source

Requirements: CMake 3.22+, a C++17 compiler (Xcode on macOS, Visual Studio 2022 on Windows),
git. JUCE is downloaded automatically. The NDI headers (MIT-licensed) are included in
`third_party/ndi`. The NDI library itself is loaded at runtime, so the NDI SDK isn't needed.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --config Release --target NDIAudioBridge
```

The app lands in `build/NDIAudioBridge_artefacts/Release/`. For a universal macOS build add
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` to the first command.

The virtual device for macOS (needs Xcode):

```bash
scripts/macos/build-virtual-device.sh
```

```bash
scripts/macos/install-virtual-device.sh build-virtual-device/NDIAudioBridge.driver
```

**Prebuilt downloads** are on the [Releases page](../../releases). The workflow in
`.github/workflows/build.yml` builds the macOS zip (app + virtual device) and the Windows zip on
every push (downloadable from the run's *Artifacts*). To publish a new release, bump the version
in `CMakeLists.txt` (`project(NDIAudioBridge VERSION 0.1.1 ...)`) and push to `main`. The
workflow creates the `v0.1.1` tag and release with both zips. Pushing a `v*` tag works too.

### Self-test

`NDIAudioBridgeSelfTest` runs without audio hardware. It simulates 10 minutes of a bursty,
drifting network feed (±300 ppm, 25/30 fps frame sizes, 48 kHz → 44.1 kHz) and checks for
zero dropouts, steady latency and a click-free signal. It also drives the real mixing engine with
two fake interfaces to check per-side routing, per-source levels, mono folding and mixing
across interfaces, and sends real OSC packets to the Companion control to check cough on/off/
toggle and the feedback that comes back. If an NDI runtime is installed, it also
sends a tone through a real NDI sender and receiver and checks it arrives intact.

### Code layout

```
src/engine/AudioEngine.*   interfaces, clock bridges between them, routing, NDI wiring
src/engine/Settings.*      save/restore (and migration from 0.1/0.2)
src/engine/SystemMonitor.* CPU and network numbers for the sidebar
src/engine/RemoteControl.* OSC control for Bitfocus Companion (cough, on-air feedback)
src/dsp/DriftFifo.h        clock-drift-correcting resampling FIFO
src/dsp/StereoFifo.h       lock-free FIFO between audio callbacks and the NDI thread
src/ndi/                   runtime NDI loader, sender hub, receiver, source finder
src/ui/                    sidebar, wire view, mixer, source pop-out, pages, theme
scripts/macos/             virtual audio device build/install
```

## Licenses

* This project's own source code is MIT-licensed (see [LICENSE](LICENSE)).
* It's built with JUCE, which is used under its open-source license (AGPLv3). So the compiled
  app as a whole falls under AGPLv3 unless you have a commercial JUCE license. For personal or
  in-house use this changes nothing; it only matters if you distribute the app.
* NDI headers in `third_party/ndi` are MIT-licensed. The NDI runtime is installed separately
  and covered by the NDI license. NDI® is a registered trademark of Vizrt NDI AB.
* The macOS virtual device is built from [BlackHole](https://github.com/ExistentialAudio/BlackHole)
  (GPL-3.0).
