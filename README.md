# Vita Moonlight

Vita Moonlight is a PlayStation Vita port of Moonlight, with major improvements for usability, pairing, device management, and now advanced touch, multitouch, and DS4 touchpad emulation.

## Highlights (0.13.2)

- **Touchscreen Modes Unified:** Select between three touchscreen modes from the settings menu:
  - **DS4 Touchpad:** Emulates a DualShock 4-style multitouch touchpad, compatible with gestures and Steam Input advanced controls.
  - **Absolute Mouse:** Use the Vita screen as a true absolute mouse (with improved gesture support).
  - **Tablet (Sunshine):** Native multitouch for Sunshine streaming, with true multi-finger gestures.
- **DS4 Touchpad Sensitivity:** DS4 mode is now more sensitive and precise for a smoother experience.
- **PS Button Capture:** Optional PS button capture implemented — double press opens the menu, single press forwarded to Sunshine as PS/Xbox button mapping (requires SceShell permission in the VPK).
- **Touch Zones Independent of Mode:** Touch zone support allows using front-screen touch zones independent of the selected touch input mode; when a zone is active, touch input in that area is ignored.
- **Wake-on-LAN (WOL) Integration:** Power on your remote PC directly from the Vita, with robust MAC address handling and cross-platform compatibility.
- **Host MAC Management:** MAC addresses are now saved/loaded correctly, with all ARP/legacy logic removed.
- **WOL Packet Debugging:** Includes a Python script (`tools/wol_sniffer.py`) to verify WOL packets on your network.
- **UI Improvements:**
  - Host Management menu: improved navigation (O/cancel returns), clearer visual feedback, and unified status display.
  - Special button overlays no longer interfere with absolute touch input.
- **Combo Fixes:** L1+L2 and R1+R2 combos now work correctly, even with touchscreen or backtouch enabled.
- **Robustness:** Many bugfixes, code cleanups, and internal refactors for stability and maintainability.
- **L1/R1 and L2/R2 Swap:** Swap the functions of the L1/R1 and L2/R2 buttons from the settings menu for greater comfort and customization.
- **Gamepad Type Selection:** Choose Xbox or PlayStation controller layout directly from the settings menu.

## Documentation

More information can find [moonlight-docs][1], [moonlight-embedded][2], and our [wiki][3].
If you need more help, join the #vita-help channel in [discord][4].

[1]: https://github.com/moonlight-stream/moonlight-docs/wiki
[2]: https://github.com/irtimmer/moonlight-embedded/wiki
[3]: https://github.com/xyzz/vita-moonlight/wiki
[4]: https://discord.gg/atkmxxT

## Upcoming Features

- **Artemis/Apollo compatibility:** Planned support for Artemis/Apollo (a modified Sunshine host), to allow streaming from more sources and custom servers.

Stay tuned for more improvements!

## How to open the Pause Menu

> **To open the pause menu at any time (even in any touch mode), press:**
> 
> **START + L + R**
>
> This shortcut works regardless of the selected touch mode (Absolute Mouse or Touchscreen). Use it to access the in-game pause/options menu quickly.

---

## How to open the Floating Keyboard

> **To open the elevated floating keyboard at any time, press:**
>
> **START + LEFT**
>
> This shortcut will always open the virtual keyboard in elevated mode, never covering the main screen. Works in all touch modes.

---


 
## What's New in 0.13.2

- **PS button capture:** When enabled, double-pressing the PS button will open the menu; a single press is passed through as a PS/Xbox button press to Sunshine.
  - Note: Implementing PS button capture required removing the `-s` (safe) flag from `vita-make-fself` and adding the `SceShell` permission (0x2800000000000001) to the VPK.
- **Touch Zones Independent of Mode:** Added a setting to enable/disable touch zones independently of the touch input mode.
  - Allows using front screen touch zones regardless of touch input mode. When a touch zone is pressed, the touch input in that area is ignored; unused touch zones work like normal touch areas.

### Bug fixes (0.13.2)

- Fixed front touch zone L2/R2 special keys not working.

### Refactor & maintenance (0.13.2)

- Updated subproject commits for `enet`, `inih`, and `moonlight-common-c`.
- Split `vitainput_process` into multiple smaller functions for readability and maintainability.

---

## Screenshots

<p align="center">
  <img src="docs/keyboard.jpg" alt="Floating keyboard in Steam app" width="400"/>
  <br><b>Floating keyboard in a Steam app using Moonlight</b>
</p>

<p align="center">
  <img src="docs/ip1.png" alt="Host waiting for IP update (yellow)" width="400"/>
  <br><b>Host waiting for IP update (yellow)</b>
</p>

<p align="center">
  <img src="docs/ip2.png" alt="Host online (green)" width="400"/>
  <br><b>Host online (green)</b>
</p>

<p align="center">
  <img src="docs/ip3.png" alt="Host offline/disconnected (red)" width="400"/>
  <br><b>Host offline/disconnected (red)</b>
</p>

<p align="center">
  <img src="docs/ip4.png" alt="IP change confirmation dialog" width="400"/>
  <br><b>IP change confirmation dialog (shows old/new IP)</b>
</p>

<p align="center">
  <img src="docs/ip5.png" alt="Search device function" width="400"/>
  <br><b>Search device function showing a found local device</b>
</p>

---


## Build Requirements

- **VitaSDK** installed and configured on your system ([guide here](https://vitasdk.org/)).
- **Submodules updated:**
  ```sh
  git submodule update --init
  ```

---

## Quick Build

1. Install dependencies with [vdpm](https://github.com/vitasdk/vdpm) if you haven't already.
2. Make sure VitaSDK is installed and in your $PATH.
3. Run:
   ```sh
   ./makepsv
   ```
   This will generate a VPK file ready to install on your PS Vita.

---

## Manual Build (optional)

If you prefer to build manually:

```sh
# If you do git pull, make sure to update submodules first
 git submodule update --init
 mkdir build && cd build
 cmake ..
 make
```

---

## Note about colors in vita2d

> **Important:** The vita2d library interprets colors in BGRA format (not RGBA). For example:
> - 0xFF00FFFF will appear **yellow** (not cyan)
> - 0xFFFFFF00 will appear **blue** (not yellow)
> - 0xFF00FF00 will appear **green** (correct)
> - 0xFFFF0000 will appear **red** (correct)
>
> If the color does not look as expected, swap the byte order (use BGR instead of RGB).


Thanks to all contributors and the Moonlight community!

