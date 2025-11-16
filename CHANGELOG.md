## 0.13.2

* added PS button capture
  - when enabled, double-pressing the PS button will open the menu, single press is passed as PS/Xbox button to Sunshine
  - This required disabling the safe (-s) flag in `vita-make-fself` and adding the `SceShell` permission (0x2800000000000001)
* allow enabling/disabling touch zones independent of touch mode (separate setting)
  - allows using front screen touch zones regardless of touch input mode. when a touch zone is pressed, the touch input is ignored. unused touch zones are ignored and function as normal touch area.

Bug fixes:
* fixed front touch zone L2/R2 special keys not working

Refactor:
* fix: Update subproject commits for enet, inih, and moonlight-common-c
* split `vitainput_process` into multiple smaller functions for better code readability

## 0.13.1

* fix: Split shortcuts into individual files for better code organization and maintainability.
* fix: Resolved ghost/phantom button presses when handling overlays and shortcuts. Button state is now correctly reset and restored.

## 0.13.0

* You can now choose between three touchscreen modes: DS4 Touchpad, Absolute Mouse, and Tablet (Sunshine) from the settings menu.
* "DS4 Touchpad" mode lets you use the Vita screen as a DualShock 4-style multitouch touchpad, compatible with gestures and advanced controls in games and Steam Input.
* DS4 Touchpad mode is now more sensitive and precise for a smoother experience.
* Wake-on-LAN (WOL) fully integrated into the host management menu: allows you to power on the remote PC from the Vita using the saved MAC and automatically calculated broadcast address.
* The host MAC address is now saved and loaded correctly from the device.ini file, with all ARP/legacy logic removed.
* WOL packet sending is robust and compatible with local networks, with cross-platform MAC parsing and detailed debug logs.
* Added Python script (`tools/wol_sniffer.py`) to receive and verify Wake-on-LAN packets on the network, useful for testing without powering off the PC.
* Minor UI fixes and improved visual feedback in the host management menu.
* Fixed: Combos like L1+L2 and R1+R2 now work correctly even when used together with either touchscreen or backtouch. Pressing both at the same time no longer causes one to be lifted; both can be held simultaneously as expected.
* Fixed: In absolute touch mode, special button overlays no longer interfere with touch input. If a special button is detected, the corresponding touchscreen input is ignored, allowing for true absolute touch and making all screen corners usable.

## 0.12.3

* Moonlight now automatically selects and creates the best available folder for config and cache (supports ux0:/data/moonlight, ux0:/moonlight, uma0:/data/moonlight, and fallback to ux0:data).
* The chosen folder location is logged at startup for easier debugging.
* The config file (moonlight.conf) is always created in the selected folder, improving compatibility for users with missing folders.
* Improved error handling and debug logging for folder creation and config file setup.
* All folder and config path logic is now centralized and robust, reducing crashes and edge cases.
* Bugfix: Prevented crashes on startup due to buffer or path issues in folder selection logic.
* New Host Management menu: manage paired hosts with improved status display, unified status text, and direct actions (connect, force connect, change IP/name, delete).
  - Now allows:
    - Changing the host name
    - Changing the host IP
    - Forcing connection even if the host is not shown as online
    - Deleting hosts
* The connection and host management menus now show the port next to the IP (uses httpPort if available, otherwise httpsPort).
* Host Management menu now shows host status (paired/unpaired, online/offline, IP changed) in a unified way, matching the main menu logic.
* Visual feedback and menu options for host management are now clearer and more robust.
* Internal refactor: main.c and check_dir.c now use a shared macro for path buffer size and a helper for folder selection.

## 0.12.2

* Added option to swap L1/R1 and L2/R2 button functions from the settings menu.
* Added option to select gamepad type: Xbox or PlayStation layout, configurable from the settings menu.
* Improved host scan logic: scan is always restarted cleanly when returning to the main menu or after leaving any submenu.
* UI and config logic for swap and mapping exclusivity is now robust and bidirectional.
* Controller type selection in UI now saves/loads correctly and updates immediately.
* Touch Mouse and Touchscreen toggles now update their subname in the menu immediately after toggling.
* Main menu always restarts host scan and waits for thread readiness before allowing host selection.
* Cleaned up and stabilized code, especially around host scan thread handling and function declarations.
* Many bugfixes and code cleanups.

## 0.12.1

* Add support for multi-touch touchscreen mode (Sunshine compatible) and absolute mouse mode.
* New settings: choose between "Touch Mouse Absolute" (gestures, mouse) and "Touchscreen (Sunshine multitouch)" (tablet mode).
* In touchscreen mode: true multitouch, native gestures (e.g. zoom), no mouse cursor/ghost, no mouse clicks.
* In absolute mouse mode: improved gestures (right click with two-finger tap, scroll with two fingers), cursor always follows main finger.
* Physical controls (gamepad) work in both modes.
* Improved logic for right click and scroll gestures.
* Many bugfixes and refinements to touch and gesture experience.

## 0.11.4

* Host status indicator dots: green (online), yellow (IP change pending), red (offline/disconnected).
* Robust host status checking with background scan thread and mDNS sniffer.
* Manual refresh of host status with TRIANGLE (△) in main menu and device search.
* IP change confirmation dialog now always shows old and new IP, and always asks for confirmation.
* Host status and menu only refresh when status actually changes or user requests it.
* Improved debug logging for all host and menu transitions.
* UI and logic are now robust even if host fields are empty or uninitialized.
* All UI messages for search device and refresh are now in English.
* Added screenshots to documentation (see docs/):
  * keyboard.png: Floating keyboard in a Steam app using Moonlight.
  * ip1.png: Host waiting for IP update (yellow).
  * ip2.png: Host online (green).
  * ip3.png: Host offline/disconnected (red).
  * ip4.png: IP change confirmation dialog (shows old/new IP).
  * ip5.png: Search device function showing a found local device.

## 0.11.2

* More reliable and user-friendly automatic pairing.
* Experimental absolute touch controls (beta).
* Floating virtual keyboard that no longer covers the main screen.
* Updated libraries and dependencies for improved compatibility and stability.
* New modular UDP mDNS sniffer for device discovery (replaces legacy mdns code).
* Improved debug logging, especially for device search and reconnect flows.
* Cleanup of legacy code and dependencies.

## 0.11.0

* The hold L to activate gyro feature has been removed and replaced with gyro reporting.
This means that the client will try to get Sunshine to emulate a DS5 controller and gyro will
be reported. This opens up full configurability with steam input. Note this only works on Windows
or on Linux with Pre-release versions of Sunshine as of 1/10/25. This could be different in the future.
* Video slicing added back
* Logging added to pairing for debug
* An icon to indicate when the double tap sprinting has been enabled has been added

## 0.10.1
* Fix pairing issue (Same one from v0.9.3)

## 0.10.0
* Add double tap to sprint option
* Add motion controls
* Fork moonlight-common-c and enet

## 0.9.3
* Fix pairing issue (#231, 5494d93)
* Update latest moonlight-common-c & enet (902cbed)
* Update latest libgamestream (#232)

## 0.9.2
* Fixed disconnects after stream is started (#222)

## 0.9.1
* Support GFE 3.22 (2452e98)

## 0.9.0
* Expose local audio setting to the end user (#173)
* Remember the currently connected address (7998108)
* Add a 21:9 resolution (1280x540) to Settings (f1eb931)
* Properly display stream in the correct display ratio, and place in the middle of the screen (14e3a2c)
* Since GFE will add blackbars to the stream even when a non-16:9 monitor is using 16:9 resolution,
    there is another option to use along the 21:9 resolution to only display the center 16:9 region (5b7a2cc)
* Detect supported resolutions (#193)
* Add new option to enable/disable of the vita vblank waiting (#197)

## 0.8.0
* Add new option for swapping O/X buttons (#168)
* Add new option for drawing FPS value (#167)
* Fix `RTSP message too long` (#164, fbe3d06)

## 0.7.0
* Support 960x540 resolution (#162, fc7e19c)
* Support GFE 3.20 (97a6a0b)

## 0.6.1
* Fix the stream delay issue (f791b3d); thanks @AlC4pwn to confirm fixing the bug

## 0.6.0
* Update latest moonlight-common-c (cfadd84, ffe8c15)
* Update latest inih (a2e38bb)
* Fix invalid variable initializations
* Apply SPS changes of moonlight-embedded
* Use nerdfont instead VITA system font (7e2b682)
* Use vita2d instead direct framebuffer handling (70330e2)
* Implement poor network indicator (749aaaf)

## 0.5.0
* Support to discover stream server via mDNS (#143)
* Implement frame pacer (#147)
* Update latest enet & moonlight-common-c (904a5d1, 7f63f0d)

## 0.4.1
* Update latest moonlight-common-c (2c9d61c)

## 0.4.0
* Sort app list alphabetically (#127)
* Improve special button settings (#130)
* Implement video cleanup for fixing connection issue (#131)

## 0.3.3
* Update latest moonlight-common-c (8e77710)
* Cherry pick upstream change about gamepad masking (933d700)
* Reduce mismatch between upstream (e2d7910, e3cad393)

## 0.3.2
* New configure option about streaming optimization (d2c974a)
* Fix cannot use 960x544 resolution (653afa6)

## 0.3.1
* Fix cannot connect with new devices (b838278)

## 0.3.0
* Support GFE 3.11
* New configure option about reference frame invalidation (#89, #91)
* Fix unset resolution configure (#90)

## 0.2.0
* Support newer vitasdk (94988ab)
* Support forward error correction (f8631b5, 184bdbe)
* No more build uncompress binary (6fd22e7)

## 0.1.2
* Compress binary (#75)
* Support GFE 3.2 (#76)

## 0.1.1
* Fix connection problem on the GFE 2.2.3 (#66)
* Improve logging datas (#67, #68)
* Support newer vitasdk (#70)

## 0.1.0
* Add new option for store debugging log (#53)
* Fix crash at quit application (#54)
* Add livearea (#62)
* Add new option for mouse acceleration (#63)
* Cleanup codes. (#60)
* Now remove alpha mark. but still lower version :) (#64)

## alpha6
* Reimplement input & config process for the improve stability (#47)
* Fix little memory leaks (#47)
* Fix crash if have too many moonlight supporting games (#47)
* Fix crash at press circle button on the connect menu (#47)
* Fix bug about not detect device model (#50)
* Fix cannot use analog sticks on the VITA 2000 (#50)
* Change priority of input thread for the high bitrate connection (#50)
* Update mapping values & add new mapping file for the PSTV (#51)

## alpha5
* Fix crash at first time (dcd1dc8)
* Improve input packet handle (#24)
* Change connection option (#26)
* Code cleanup for input handling (#27, #30, #31)
* Support L2/R2/L3/R3 Buttons on PSTV (#36)
* Update build toolchains & Fix connection problems under GFE 3.0 (#39)

## alpha4
* Support GUI (#13)
* Support editing config file (#4)
* Support input mapping (#5)
* Add new config options for power saving (#6)
* Turn safe application (#7)

## alpha3
* Support virtual button for L2/R2/L3/R3 using touchscreen (#1, #2)
* Support mouse move and click using touchscreen (#3)

## alpha2
* Support custom settings; screen resolution, framerate and bitrate (d145403)

## alpha1
* Initial release (04a7c1d)
