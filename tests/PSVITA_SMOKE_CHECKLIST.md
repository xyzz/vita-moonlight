# PS Vita / PSTV manual release smoke tests

These checks complement the automated tests and `scripts/check_vpk.py`.
They **must be performed on hardware** before declaring a public release ready.
The CI build validates packaging, not real streaming, decoding, audio or controls.

Record the VPK build SHA, PS Vita/PSTV model, firmware/homebrew environment,
Sunshine version, network type, and a short PASS/FAIL plus evidence for each case.
Use a test installation; back up configuration and host pairing files first.

## Installation and frontend selection

- [ ] Install the **single** `moonlight.vpk` using VitaShell; confirm one LiveArea entry.
- [ ] On a fresh test setup (after backing up `ux0:data/moonlight/interface.conf`),
      confirm the first-run dialog fits on screen and displays the Legacy/Borealis versions.
- [ ] Choose **Yes**: Borealis launches, then restart the application and confirm the choice persists.
- [ ] Reset only the frontend-selection file, choose **No**: Legacy launches and the choice persists.
- [ ] Switch **Legacy -> Borealis -> Legacy** from Settings > Interface. No crash or settings corruption.
- [ ] Existing paired hosts stay available after switching; frontend-specific preferences remain independent.
- [ ] Update/install over the previous VPK and confirm the saved selection and pairing still work.

## Functional checks — repeat for both interfaces

- [ ] Find a Sunshine host on the local network and pair a fresh test host.
- [ ] Connect to a previously paired host, exit, and reconnect; test host unavailable/timeout handling.
- [ ] Attempt to start a stream while the host is unavailable. The failed attempt should return to the menu without leaving a stale active-session state; then reconnect and stream normally.
- [ ] Stream H.264 at 480p and 720p (or modes supported by the tested Vita/host),
      checking image orientation, frame pacing and absence of sustained drift.
- [ ] Test automatic bitrate at 960x544 and 1280x720, then select a fixed bitrate and verify that the host receives the expected settings.
- [ ] Confirm game audio works, remains in sync and survives pause/reconnect.
- [ ] Confirm face buttons, D-pad, sticks, shoulders and configurable shortcuts.
- [ ] Confirm front touch and rear touch as configured; on PSTV mark touch-specific cases N/A.
- [ ] Open and use the virtual keyboard or text input, then return to streaming.
- [ ] Suspend/resume the Vita during streaming and verify recovery of video, audio and controller state. Repeat with the host disconnected during suspension.
- [ ] Briefly interrupt Wi-Fi or stop Sunshine, then verify a controlled error and successful reconnect.
- [ ] Exit streaming to the app menu and close the app without freezing.

## Optional features (only when configured)

- [ ] Moonmic forwards microphone audio without breaking local playback.
- [ ] Wake-on-LAN starts an eligible host; check behavior when WOL is unavailable.
- [ ] Motion controls and customized input mappings behave as configured.
- [ ] Long session: monitor stability, temperature and audio/video synchronization.

## Failure evidence

For each failure, record the interface (Legacy or Borealis), exact reproduction steps,
resolution/FPS, host and Vita logs, and a screenshot/video if relevant.
Do not treat a passing VPK packaging check as proof that these manual cases passed.

Keep these results with the release review; packaging and CI checks alone do not establish hardware compatibility.
