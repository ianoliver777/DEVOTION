# DEVOTION 1.1.0

**THE TRINITYKNOBS COLLECTION**  
By **TRIPLE SEVEN AUDIO**  
Vocal polish

## What's in the download

```
DEVOTION-1.1.0/
├── INSTALL.md            ← this guide
├── macOS/
│   ├── VST3/DEVOTION.vst3
│   └── AU/DEVOTION.component
└── Windows/
    └── DEVOTION.vst3
```

Extract the ZIP first, then follow the section for your computer.

## macOS (Apple Silicon and Intel)

The `macOS` folder contains universal VST3 and Audio Unit plugins. They run natively on Apple Silicon and Intel Macs with macOS 11 or later.

1. Quit your DAW and extract the ZIP.
2. In Finder, choose **Go → Go to Folder** (Shift–Command–G) and open the folder for your format. If a folder is missing, create it under `~/Library/Audio/Plug-Ins`.

   | Format | Copy this entire item | To this folder |
   | --- | --- | --- |
   | VST3 | `macOS/VST3/DEVOTION.vst3` | `~/Library/Audio/Plug-Ins/VST3/` |
   | Audio Unit | `macOS/AU/DEVOTION.component` | `~/Library/Audio/Plug-Ins/Components/` |

3. Reopen your DAW and rescan. In **Ableton Live**, turn on VST3 System Folders or Audio Units in **Settings → Plug-Ins**, then click **Rescan**. In **Logic Pro**, use the Audio Unit. If it doesn't show up, open **Logic Pro → Settings → Plug-in Manager**, select DEVOTION and click **Reset & Rescan Selection**.
4. Find **DEVOTION** under **TRIPLE SEVEN AUDIO** and put it on your vocal.

**If macOS blocks the plugin:** it isn't notarized by Apple yet. Open **System Settings → Privacy & Security** and click **Open Anyway**, or quit your DAW and run the line for the format you installed in Terminal:

```sh
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/DEVOTION.vst3"
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/Components/DEVOTION.component"
```

## Windows x64

1. Quit your DAW. Copy the entire `Windows/DEVOTION.vst3` folder into `C:\Program Files\Common Files\VST3\`. Windows may ask for administrator permission.
2. Reopen your 64-bit VST3 host and rescan. In Ableton Live, turn on **VST3 System Folders** in **Settings → Plug-Ins**, then click **Rescan**.
3. Put **DEVOTION** on your vocal track or vocal bus.

**If Windows blocks the ZIP:** before extracting, right-click it, choose **Properties**, tick **Unblock** and click **OK**.

## Controls

- **AIR** opens up the top end: a high shelf of up to +5 dB around 12 kHz plus a subtle, level-aware sparkle above 6 kHz.
- **PRESENCE** brings the vocal forward: a +4 dB lift around 3.2 kHz plus gentle soft-knee levelling (2:1 up to 3:1, 5 ms attack, 80 ms release) that evens out loud and quiet words. The **COMP** readout under the knob shows how much it's working.
- **SILK** smooths the vocal: a split-band de-esser that turns down the top end (above 5.5 kHz) only while an "s", "sh" or "t" is happening, by up to 12 dB, plus a gentle dip in the harsh 4–5 kHz zone. The **DE-ESS** readout shows the cut.
- **MIX** (the slider under the preset bar) blends the processed signal with the dry input for parallel processing. 100% is fully wet. Double-click it to return to 100%.
- **MATCH** sets the processed result to your input's loudness (up to 6 dB either way), so you can judge the tone without being fooled by level changes.
- Presets: **Init**, **Pop Sheen**, **Rap Upfront**, **Soft & Silky**, **Breathy Air**. The disk icon saves a `.t7apreset` file and **••• → Open preset file** loads one. **••• → Bypass** toggles the plugin's bypass. Your DAW project recalls every control.

DEVOTION has zero latency. At Init (all knobs at 0%) it passes audio through unchanged.

To uninstall, quit your DAW, delete the plugin you copied and rescan.
