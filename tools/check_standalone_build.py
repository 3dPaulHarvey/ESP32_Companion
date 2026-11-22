"""Verify the standalone image and saved recovery file before guarded flashing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re

from check_device_auth import load_token


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", action="store_true")
    parser.add_argument("--build", type=Path, default=Path(os.environ["LOCALAPPDATA"]) / "ESP32_Companion/build-standalone")
    parser.add_argument("--report", type=Path, help="Save this check separately from earlier preflash evidence")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    fw = root / "Source/esp32-companion"
    build = args.build.resolve()
    metadata = json.loads((build / "project_description.json").read_text(encoding="utf-8"))
    assert Path(metadata["build_dir"]).resolve() == build, "Build metadata belongs to another directory"
    assert Path(metadata["project_path"]).resolve() == fw.resolve(), "Build metadata belongs to another firmware source"
    assert metadata["project_name"] == "esp32_companion", "Expected the renamed companion project"
    config_file = Path(metadata["config_file"]).resolve()
    assert config_file.parent == build, "The selected build must own its generated sdkconfig"
    app_binary = metadata["app_bin"]
    assert app_binary == "esp32_companion.bin", "Expected the renamed companion firmware artifact"
    image_file = (build / app_binary).resolve()
    assert image_file.parent == build, "Application binary must belong to the selected build"
    image = image_file.read_bytes()
    config = config_file.read_text(encoding="utf-8")
    for field in ["CONFIG_COMPANION_STANDALONE=y", "CONFIG_COMPANION_FOLLOWUP=y", "CONFIG_COMPANION_CLOUD_WAKE=y",
            "CONFIG_COMPANION_GEMINI_QUESTION_PROBE=y", "CONFIG_COMPANION_MICROPHONE_SHIFT=15",
            "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y", "CONFIG_MBEDTLS_HAVE_TIME_DATE=y", "CONFIG_LOG_MAXIMUM_LEVEL=3",
            "CONFIG_SR_WN_WN9_HIESP=y", "CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y"]:
        assert field in config, field
    for field in ["CONFIG_LWIP_TCP_SND_BUF_DEFAULT=5760", "CONFIG_LWIP_TCP_WND_DEFAULT=5760", "CONFIG_LWIP_TCP_RECVMBOX_SIZE=6"]:
        assert field in config, field
    assert "CONFIG_ESP_TLS_INSECURE=y" not in config
    assert "CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y" not in config
    commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    transports = [c for c in commands if "/tcp_transport/" in c["file"].replace("\\", "/")]
    assert len(transports) == 5 and all("-DLOG_LOCAL_LEVEL=0" in c["command"] for c in transports)
    assert not any(x in image for x in [b"Error write Upgrade header", b"Write upgrade request", load_token().encode()])
    assert b"FEATURE version=1 boot_wifi=1 auto_wake=%d max_turns=2 pc_heartbeat=0 control_lease_ms=10000 failure_cooldown_ms=30000" in image
    assert (b"FIXTURE_FEATURE enabled=1" in image) == args.fixture
    assert (b"FIXTURE_BEGIN" in image) == args.fixture
    assert any(c["file"].replace("\\", "/").endswith("/question_fixture.cc") for c in commands) == args.fixture
    for fixture in [root / "logs/robot-question-public-fixture.pcm", root / "logs/robot-followup-public-fixture.pcm"]:
        assert (fixture.read_bytes() in image) == args.fixture
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    configured_sdkconfig = re.search(r"^SDKCONFIG:[^=]+=(.+)$", cache, re.MULTILINE)
    if configured_sdkconfig:
        cache_config_file = Path(configured_sdkconfig[1].strip())
        if not cache_config_file.is_absolute():
            cache_config_file = fw / cache_config_file
        assert cache_config_file.resolve() == config_file, "CMake and build metadata disagree about sdkconfig"
    assert re.search(r"COMPANION_QUESTION_FIXTURE:(?:UNINITIALIZED|BOOL)=" + ("ON" if args.fixture else "OFF"), cache)
    flash = json.loads((build / "flasher_args.json").read_text(encoding="utf-8"))
    assert "0x9000" not in flash["flash_files"]
    backup = root / "recovery/2026-11-18-phase-5c/before-standalone-full-flash.bin"
    raw = backup.read_bytes(); backup_sha = hashlib.sha256(raw).hexdigest()
    assert len(raw) == 8388608 and backup_sha == "7f78a1f2632da24f51c66aac466cb3c47fc65fb6604d0e81d1cc971c29507e42"
    previous = root / "recovery/2026-11-18-phase-5b/built-followup"
    previous_flash = json.loads((previous / "flasher_args.json").read_text(encoding="utf-8"))
    previous_app = (previous / previous_flash["app"]["file"]).resolve()
    assert previous_app.parent == previous.resolve(), "Archived application must belong to the recovery directory"
    assert int(previous_flash["app"]["offset"], 0) == 0x20000
    previous_image = previous_app.read_bytes()
    assert raw[0x20000:0x20000 + len(previous_image)] == previous_image
    tests = {}
    for path in (fw / "tests").glob("companion_*policy_test.cpp"):
        assert any(Path(c["file"]).resolve() == path.resolve() for c in commands), path.name
        tests[path.name] = len(re.findall(r"static_assert\(", path.read_text(encoding="utf-8")))
    assert sum(tests.values()) == 206, tests
    assert b"WRITE_POLICY version=1 resume_partial=1 frame_timeout_ms=1000 question_retries=0" in image
    assert (b"WRITE_FAULT injected=" in image) == args.fixture
    assert (b"LIVE_FIXTURE_BEGIN" in image) == args.fixture
    assert b"LIVE_FEATURE version=1 connection_during_capture=1 warm_wake=1 automatic_replay=0" in image
    assert b"WIFI_RADIO power_save=0 performance=%d" in image
    report = {"firmware_bytes": len(image), "firmware_sha256": hashlib.sha256(image).hexdigest(),
        "backup_bytes": len(raw), "backup_sha256": backup_sha, "backup_contains_verified_followup": True,
        "secure_configuration": True, "private_transport_logs_compiled_out": 5, "device_credential_absent_from_image": True,
        "nvs_preserved": True, "fixture_disabled": not args.fixture, "capture_shift": 15, "wake_shift": 12,
        "capture_samples": 64000, "boot_wifi": True, "auto_wake": not args.fixture, "pc_heartbeat_required": False,
        "max_conversation_turns": 2, "failure_cooldown_ms": 30000, "assertions": tests,
        "live_max_conversation_turns": 8, "live_end_silence_ms": 600, "live_capture_max_ms": 10000,
        "live_quiet_wait_ms": 4000, "live_idle_ms": 30000, "live_session_ms": 120000,
        "radio_power_saving_disabled": True, "live_fixture_excluded": not args.fixture,
        "tcp_send_buffer_bytes": 5760, "tcp_receive_window_bytes": 5760, "tcp_receive_mailbox": 6,
        "compiled_assertions": sum(tests.values()), "build_directory": str(build),
        "project_name": metadata["project_name"], "app_binary": app_binary,
        "sdkconfig_file": str(config_file)}
    out = args.report or root / ("logs/esp32-standalone-" + ("fixture-" if args.fixture else "") + "preflash.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__": main()
