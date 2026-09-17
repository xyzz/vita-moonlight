## 1.0.0

- Replaced the legacy interface with Borealis.
- Added FFmpeg-vita hardware decoding and direct GXM presentation.
- Added 480p through 768p stream modes, automatic bitrate and in-stream display controls.
- Added front and rear touch zones, gyroscope settings and configurable shortcuts.
- Added legacy and modern virtual keyboards with selectable layouts.
- Added Moonmic microphone forwarding, diagnostics and host monitor resolution control.
- Added Japanese, Brazilian Portuguese and Traditional Chinese translations.
- Resynchronize stale decoder output instead of accumulating playback delay.
- Handle variable-size Opus frames and restore local audio playback.
- Keep active touch zones separate from mouse and tablet input.
- Migrate absolute mouse settings from earlier releases.
- Reconnect the stream and restore controller state after application resume.
- Use the same build scripts on Linux, WSL and Windows/MSYS2.
- Install VitaSDK packages and the pinned FFmpeg-vita fork with `scripts/prepare/install_all.sh`.
- Apply the Borealis Vita patch during CMake configuration.

## 0.13.2

- Added optional PS button capture: double press opens the menu; single press is forwarded to Sunshine.
- Added independent touch-zone settings.
- Fixed front touch zones for L2/R2.
- Store host MAC addresses instead of querying them through ARP.
- Updated enet, inih and moonlight-common-c.

## 0.13.1

- Reset and restore button state when switching overlays and shortcuts to prevent phantom presses.

## 0.13.0

- Added DS4 Touchpad, Absolute Mouse and Tablet (Sunshine) touchscreen modes.
- Increased DS4 touchpad sensitivity.
- Added Wake-on-LAN to the host menu using the MAC address stored in `device.ini`.
- Added `tools/wol_sniffer.py` to inspect Wake-on-LAN packets.
- Allow L1+L2 and R1+R2 to remain held together when using touch or rear touch.
- Ignore mouse input in areas occupied by special-button overlays.

## 0.12.3

- Select and create a configuration directory on ux0 or uma0, and log the selected path at startup.
- Create `moonlight.conf` in the selected directory.
- Prevent startup crashes caused by truncated configuration paths.
- Added a host menu to connect, force connect, rename, change IP or delete a host.
- Show the connection port beside the host IP.
- Show paired, online and pending-IP states in the host menu.

## 0.12.2

- Added shoulder-button swapping and Xbox/PlayStation gamepad layout selection.
- Restart host discovery after returning to the main menu.
- Keep shoulder-button swap and controller mapping settings synchronized.
- Save controller type changes and update touch-mode labels immediately.
- Wait for the discovery thread before enabling host selection.

## 0.12.1

- Added Sunshine multitouch and absolute mouse modes.
- Added two-finger right click and scrolling in absolute mouse mode.
- Keep physical gamepad controls active in both touch modes.

## 0.11.4

- Show green, yellow and red indicators for online, pending-IP and offline hosts.
- Check host status with a background discovery thread and mDNS sniffer.
- Refresh hosts with TRIANGLE in the main menu and device search.
- Show old and new addresses before confirming an IP change.
- Refresh the menu only when host status changes or on request.
- Handle empty host fields in status checks.
- Translate device search and refresh messages to English.

## 0.11.2

- Added automatic pairing and experimental absolute touch controls.
- Added a floating virtual keyboard.
- Replaced legacy discovery code with a UDP mDNS sniffer.
- Added logs for device search and reconnection failures.

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
