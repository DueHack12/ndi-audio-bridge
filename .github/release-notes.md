## What's new in 0.4.0

- **Bitfocus Companion control.** Cough-mute any mic from a Stream Deck button (on, off, toggle, or press-to-cough), with feedback that lights the button only while that mic is really going to vMix: NDI send on, cough off, interface running and vMix connected. Uses Companion's built-in **Generic: OSC** connection on UDP port 9500.
- **New Companion page** in the sidebar: connection settings, this computer's IP, the last command received, step-by-step Companion setup, and every mic's OSC paths with copy buttons and a live status.
- Coughed sources now show a red **COUGH** tag in the Routing view, and the source pop-out follows cough changes made from Companion.
- Better contrast for red buttons in dark mode.

## What's new in 0.3.1

- Fixed the interface chips overlapping the **Add interface** button in the Routing header (seen on macOS with longer NDI version text).

## What's new in 0.3.0

- **New interface:** sources and headphone mixes connected by wires (drag a source onto a mix to add it), a channel-strip mixer for the selected mix, and a pop-out panel when you click a source.
- **Several audio interfaces at once** (up to four). Every interface's inputs go out over NDI, and any mix can use inputs from any interface.
- **A level for each source in each mix**, plus L/R per source in stereo mixes.
- **Light and dark mode** (Settings, or the button at the bottom of the sidebar).
- **Resource glance** in the sidebar: CPU used by the app and the whole computer, and NDI and network traffic with how much of the link is free.
- **Inputs used / outputs used** per interface, to hide channels you don't need.
- Settings from 0.1/0.2 carry over.

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
4. Open the app and allow Microphone and Local Network. The TASCAM is picked automatically (see **Interfaces** in the sidebar). Click **Computer Audio** and choose **NDI Audio Bridge** under *Plays from*.

**Windows**
1. Install NDI Tools (https://ndi.video/tools/), the TASCAM ASIO driver, and VB-Audio Virtual Cable.
2. Set Windows' output to **CABLE Input** and run the app. Under **Interfaces**, choose the TASCAM with the **ASIO** driver. Click **Computer Audio** and choose **CABLE Output** under *Plays from*.

Turn the TASCAM's **MONITOR MIX** knob fully to **COMPUTER** so the headphones only hear the app's mixes.
Full setup, vMix configuration and troubleshooting: see the [README](https://github.com/DueHack12/ndi-audio-bridge#readme).
