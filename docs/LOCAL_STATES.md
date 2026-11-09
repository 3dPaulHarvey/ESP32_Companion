# Local companion states — first increment

This is the first small Phase 2 change: replace the diagnostic loop's numeric wake phases with an explicit controller and verify its transitions. It still uses the cloud-disabled diagnostic profile.

```text
IDLE -> WAKE_DETECTED -> LISTENING -> SPEAKING -> IDLE
```

The states currently rehearse eye/cue timing: 100 ms in WAKE_DETECTED, 400 ms in LISTENING, and 800 ms in SPEAKING with the existing local popup sound. **LISTENING does not capture a microphone segment yet.** Bounded PCM capture is the next increment; input headroom calibration also remains open. The timed speaking state is not a measurement of exact I2S playback completion.

## Controller contract

- Only IDLE accepts a new wake.
- Each tick advances at most one state. Delayed ticks preserve every state and give the next state its full dwell time.
- The main diagnostic task owns transitions, eye changes, sound and wake re-arming. The AFE callback only increments an atomic notification count.
- A cycle completes only on SPEAKING -> IDLE. Detected wakes, accepted voice cycles, accepted serial cycles, and completed cycles are separate counters.
- `d` and `t` face/cue commands are ignored while a cycle is active.
- Wi-Fi events do not control the local state machine. Cloud initialization remains disabled in this profile.

The existing production `DeviceStateMachine` remains the legacy application's state machine. The local controller avoids its activation/protocol-dependent paths.

## Build and run

```powershell
.\tools\idf.ps1 -Action build -Profile diagnostics
.\tools\idf.ps1 -Action flash -Profile diagnostics -Port COM26
python .\tools\capture_serial.py --port COM26 --reset --seconds 25 --name phase2a-boot
```

The diagnostic menu adds `n`, an explicitly synthetic serial trigger. `s` reports both hardware status and local state/counters. Normal voice detection still uses “Hi, E-S-P.”

Expected serial evidence:

```text
LocalCompanion: STATE IDLE mode=timed-ui-rehearsal capture=disabled cloud=disabled
LocalCompanion: STATE IDLE -> WAKE_DETECTED cycle=1 source=voice
LocalCompanion: STATE WAKE_DETECTED -> LISTENING cycle=1 source=voice
LocalCompanion: STATE LISTENING -> SPEAKING cycle=1 source=voice
LocalCompanion: STATE SPEAKING -> IDLE cycle=1 source=voice
LocalCompanion: CYCLE_COMPLETE cycle=1 completed=1 source=voice ...
```

## Verification

The pure C++ controller tests cover boundary timing, duplicate wakes in every busy state, delayed ticks, ten complete cycles, reset and state names. Core invariants also run as compile-time `static_assert` checks in the ESP32 build.

On this Windows setup, from the repository root:

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
New-Item -ItemType Directory -Force .tools | Out-Null
g++ -std=c++17 -Wall -Wextra -Werror -pedantic Source\xiaozhi-esp32-2.2.2\tests\local_companion_cycle_test.cpp -o .tools\local_companion_cycle_test.exe
.\.tools\local_companion_cycle_test.exe
```

Hardware automation, with the diagnostic firmware idle and no one speaking the wake phrase:

```powershell
python .\tools\test_local_cycles.py --port COM26 --cycles 10 --name phase2a-serial-cycles
```

This verifies ordered transitions, minimum dwell times, busy-command rejection, counters and IDLE recovery. These are **synthetic trigger cycles**, not evidence of microphone recognition. Follow with ten real wake attempts, waiting for the idle face between each, and inspect `source=voice` completions separately.

For a network interruption check, connect with the existing `w` command first, then:

```powershell
python .\tools\test_local_cycles.py --port COM26 --cycles 1 --disconnect --name phase2a-network-cycle
```

Finally reboot and verify IDLE, zeroed counters, the local model ready and Wi-Fi off. Keep the saved Phase 1 image and [baseline recovery instructions](BUILD_AND_FLASH.md) available. Physical access-point toggling remains skipped as previously requested.

Results are recorded in [TEST_LOG.md](TEST_LOG.md). This increment does not complete Phase 2's bounded microphone capture task.
