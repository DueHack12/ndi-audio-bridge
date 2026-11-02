## What's new in 0.2.0

- **Separate L/R control in the headphone matrix.** Every cell now routes to a single output channel.
- **STEREO / 2× MONO per output pair.** Use an output pair as one stereo mix, or as two independent mono mixes with their own names, levels and meters.
- Stereo sources (vMix return, computer audio) keep their sides in stereo pairs and fold to mono in 2× mono.
- Shift-click a matrix cell to set both sides at once.
- Settings from 0.1.0 carry over (each old route is applied to both sides).

## Downloads

| File | For |
|---|---|
| **NDI-Audio-Bridge-macOS.zip** | macOS 11+, Apple Silicon and Intel. App + "NDI Audio Bridge" virtual audio device + installer |
| **NDI-Audio-Bridge-Windows.zip** | Windows 10/11, x64 |

## Quick start

**Mac**
1. Install the NDI Runtime: https://ndi.link/NDIRedistV6Apple
2. Unzip, then clear the download flag (these builds aren't notarized):
   `xattr -dr com.apple.quarantine ~/Downloads/"NDI Audio Bridge"`
3. Double-click **Install Virtual Audio Device.command**, then set System Settings › Sound › Output to **NDI Audio Bridge**.
4. Open the app, allow Microphone and Local Network, choose the interface under **Audio Interface...** and **NDI Audio Bridge** under **Computer Audio**.

**Windows**
1. Install NDI Tools (https://ndi.video/tools/), the TASCAM ASIO driver, and VB-Audio Virtual Cable.
2. Set Windows' output to **CABLE Input**, run the app, choose the TASCAM via **ASIO**, and **CABLE Output** under **Computer Audio**.

Turn the TASCAM's **MONITOR MIX** knob fully to **COMPUTER** so the headphones only hear the app's matrix.
Full setup, vMix configuration and troubleshooting: see the [README](https://github.com/DueHack12/ndi-audio-bridge#readme).
