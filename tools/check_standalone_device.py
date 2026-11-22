"""Development-board standalone regression; never uploads live microphone audio."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import time

from check_esp32_microphone import MicrophoneDevice
from check_esp32_session import CheckFailure, EXPECTED_MAC
from microphone_calibration_robot import RobotClient
from robot_standalone_client import StandaloneObserver, FEATURE, STATUS

LAST = (r"Standalone: LAST_CONVERSATION valid=([01]) job=(\d+) ok=([01]) http=(\d+) reason=([a-z_]+) "
    r"turns=(\d+) quiet_discards=(\d+) input_bytes=(\d+) played_samples=(\d+) close_ack=([01]) close_code=(\d+) fixture=([01]) owner=(desktop|standalone)")


class GuardedDevice(MicrophoneDevice):
    """Cancel unexpected real question captures before they can reach a network job."""
    def read(self):
        super().read()
        if "CompanionQuestion: CAPTURE_REQUESTED" in self.text:
            self.send(b"x")
            raise CheckFailure("An unexpected live capture was cancelled; unattended checks stopped.")


def snapshot(device):
    start = device.send(b"s")
    match = device.wait(LAST, start)
    names = ["valid", "job", "ok", "http", "reason", "turns", "quiet_discards", "input_bytes", "played_samples", "close_ack", "close_code", "fixture", "owner"]
    return {key: value if key in {"reason", "owner"} else int(value) for key, value in zip(names, match.groups())}


def connected(device):
    start = device.send(b"s")
    return device.wait(r"HardwareDiagnostics: STATUS wakes=\d+ WiFi=on connected=1", start, timeout=25)


def wait_network(device, timeout=30):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        start = device.send(b"s")
        match = device.wait(r"HardwareDiagnostics: STATUS wakes=\d+ WiFi=(on|off) connected=([01])", start)
        if match.groups() == ("on", "1"): return
        time.sleep(.2)
    raise CheckFailure("Saved Wi-Fi did not reconnect.")


def fixture_check(device, checks):
    start = device.send(b"s")
    device.wait(FEATURE.replace("auto_wake=1", "auto_wake=0"), start)
    wait_network(device)
    if "REQUEST_BEGIN" in device.text: raise CheckFailure("Boot contacted a provider unexpectedly.")
    checks.append({"case": "fixture boot connects saved Wi-Fi without cloud request or automatic microphone upload", "passed": True})
    old = snapshot(device)
    start = device.send(b"f")
    device.wait(r"CompanionConversation: OWNER mode=standalone heartbeat_required=0", start, timeout=25)
    device.wait(r"CompanionConversation: INPUT turn=1 bytes=128000 chunks=100 stream_ended=1 input_cleared=1", start, timeout=50)
    device.connection.close()
    # No serial reader, control heartbeat or PC playback for this entire interval.
    time.sleep(45)
    device.connection.open(); device.protocol()
    end = time.monotonic() + 90
    while time.monotonic() < end:
        value = snapshot(device)
        if value["valid"] and value["job"] > old["job"]: break
        time.sleep(.5)
    else: raise CheckFailure("The USB-disconnected synthetic job did not finish.")
    if (value["ok"], value["http"], value["reason"], value["turns"], value["quiet_discards"], value["input_bytes"],
            value["close_ack"], value["close_code"], value["fixture"], value["owner"]) != (1, 200, "quiet_timeout", 2, 1, 256000, 1, 1000, 1, "standalone"):
        raise CheckFailure("The USB-disconnected job failed its turn/input/close checks.")
    if not value["played_samples"]: raise CheckFailure("The disconnected job did not complete speaker playback.")
    start = device.send(b"s")
    device.wait(r"CompanionVoice: STATUS active=0 speaking=0 wake_armed=1 playback_busy=0", start)
    device.wait(r"LocalCompanion: STATUS state=IDLE .*pending=0", start)
    if "OWNER_EXPIRED" in device.text or "MicrophonePrep: RECORDING" in device.text:
        raise CheckFailure("The synthetic ownerless test expired or opened the microphone.")
    checks.append({"case": "two synthetic inputs and replies finish without serial or heartbeat", "passed": True,
        "serial_disconnected_seconds": 45, "live_microphone_uploaded": False, "result": value})


def normal_check(device, checks):
    start = device.send(b"s"); device.wait(FEATURE, start); wait_network(device)
    if "REQUEST_BEGIN" in device.text: raise CheckFailure("Boot contacted the provider without a question.")
    checks.append({"case": "saved Wi-Fi at boot with wake ready and zero provider requests", "passed": True})
    start = device.send(b"f")
    device.send(b"s"); device.wait(FEATURE, start)
    if "FIXTURE_BEGIN" in device.text[start:]: raise CheckFailure("Normal image exposes synthetic audio.")
    checks.append({"case": "normal firmware excludes synthetic command", "passed": True})

    start = device.send(b"p"); device.wait(r"Standalone: CONTROL paused=1 lease_ms=10000", start)
    start = device.send(b"o"); device.wait(r"HardwareDiagnostics: WIFI off;", start)
    start = device.send(b"s"); device.wait(r"Standalone: STATUS paused=1 wifi_wanted=1", start)
    end = time.monotonic() + 12; last = 0
    while time.monotonic() < end:
        if time.monotonic() - last > 2: device.send(b"="); last = time.monotonic()
        device.read()
    start = device.send(b"s"); device.wait(r"Standalone: STATUS paused=1 wifi_wanted=1", start)
    start = device.send(b"y"); device.wait(r"Standalone: CONTROL paused=0 wifi_wanted=1", start); wait_network(device)
    checks.append({"case": "diagnostic pause renews and restores prior Wi-Fi preference", "passed": True, "heartbeat_test_seconds": 12})

    start = device.send(b"p"); device.wait(r"Standalone: CONTROL paused=1", start)
    start = device.send(b"o"); device.wait(r"HardwareDiagnostics: WIFI off;", start)
    device.connection.close(); time.sleep(12); device.connection.open(); device.protocol(); wait_network(device)
    start = device.send(b"s"); device.wait(r"Standalone: STATUS paused=0 wifi_wanted=1", start)
    checks.append({"case": "lost diagnostic USB releases lease and reconnects without a cloud request", "passed": True, "serial_disconnected_seconds": 12})

    start = device.send(b"o"); device.wait(r"HardwareDiagnostics: WIFI off;", start)
    device.send(b"y"); time.sleep(.5)
    start = device.send(b"s"); device.wait(r"Standalone: STATUS paused=0 wifi_wanted=0", start)
    device.wait(r"HardwareDiagnostics: STATUS wakes=\d+ WiFi=off connected=0", start)
    device.send(b"w"); wait_network(device)
    checks.append({"case": "manual offline preference persists until explicit Wi-Fi resume", "passed": True})

    if "REQUEST_BEGIN" in device.text: raise CheckFailure("An unattended cloud request occurred during network checks.")
    device.connection.close()
    calibrator = RobotClient(lambda *args: None)
    try:
        policy = calibrator.connect()
        if not calibrator.device.standalone_paused: raise CheckFailure("Calibrator did not pause autonomy.")
        if policy["pcm_shift"] != 15: raise CheckFailure("Calibrated gain changed.")
        cap = calibrator.capture("quiet")
        if "REQUEST_BEGIN" in calibrator.device.text: raise CheckFailure("Local calibration opened a network job.")
        checks.append({"case": "existing calibrator pauses autonomy and captures locally", "passed": True,
            "samples": cap["samples"], "pcm_shift": cap["pcm_shift"], "clipped": cap["clipped"]})
    finally: calibrator.close()
    device.connection.open(); device.protocol(); wait_network(device)
    start = device.send(b"s"); device.wait(r"Standalone: STATUS paused=0 wifi_wanted=1", start)
    checks.append({"case": "closing calibrator restores autonomous wake and saved Wi-Fi", "passed": True})
    device.connection.close()
    observer = StandaloneObserver(lambda *args: None)
    try:
        policy = observer.connect()
        if not policy["observer_only"]: raise CheckFailure("Observer requested active controls.")
        commands = observer.device.status_commands
    finally: observer.close()
    device.connection.open(); device.protocol(); wait_network(device)
    start = device.send(b"s"); device.wait(r"Standalone: STATUS paused=0 wifi_wanted=1 cooldown_ms=0 admission_open=1", start)
    device.wait(r"CompanionVoice: STATUS active=0 speaking=0 wake_armed=1 playback_busy=0", start)
    if "REQUEST_BEGIN" in device.text: raise CheckFailure("The observer initiated an unexpected cloud job.")
    checks.append({"case": "read-only observer connects and closes without controlling the robot", "passed": True,
        "status_commands": commands, "heartbeat_commands": 0, "arm_commands": 0, "network_commands": 0})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", action="store_true")
    parser.add_argument("--report", type=Path, help="Save results separately from earlier hardware evidence")
    args = parser.parse_args()
    device = GuardedDevice(); checks = []; failure = None; stage = "identity"
    try:
        device.connection.open()
        configured, busy = device.protocol(require_capture=True)
        if not configured or busy or device.capture["pcm_shift"] != 15: raise CheckFailure("Development board is not configured and idle.")
        stage = "standalone boot"
        device.reset(); device.text = ""
        device.wait(r"HardwareDiagnostics: MODE standalone companion; cloud=[a-z-]+; WiFi=auto", 0, timeout=30)
        device.protocol()
        stage = "fixture checks" if args.fixture else "normal checks"
        (fixture_check if args.fixture else normal_check)(device, checks)
    except Exception as error:
        failure = str(error) if isinstance(error, CheckFailure) else type(error).__name__
    finally:
        device.connection.close()
        report = {"recorded_at": datetime.now(timezone.utc).isoformat(), "device_mac": EXPECTED_MAC, "fixture": args.fixture,
            "checks": checks, "failure": failure, "stage": stage, "live_microphone_audio_uploaded": False, "raw_audio_saved": False}
        name = "esp32-standalone-" + ("fixture-" if args.fixture else "") + "hardware-qa.json"
        report_path = args.report or Path(__file__).resolve().parent.parent / "logs" / name
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))
    if failure: raise SystemExit(1)


if __name__ == "__main__": main()
