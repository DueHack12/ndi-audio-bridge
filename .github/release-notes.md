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
