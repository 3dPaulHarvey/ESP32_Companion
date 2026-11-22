"""Repeated public synthetic uploads; no room microphone input or transcript."""
from pathlib import Path
import argparse, hashlib, json, re, sys, time
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'tools'))
from check_esp32_microphone import MicrophoneDevice
from check_esp32_session import CheckFailure
from check_standalone_device import GuardedDevice, wait_network
from microphone_calibration_core import atomic_json, utc_now
from robot_conversation_client import RESULT, conversation_metadata
from robot_standalone_client import FEATURE

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(); parser.add_argument('--name', required=True); parser.add_argument('--cycles', type=int, default=5)
parser.add_argument('--require-recovery', action='store_true')
args = parser.parse_args(); assert re.fullmatch(r'[a-z0-9-]+', args.name) and 1 <= args.cycles <= 12
out = root / 'logs' / (args.name + '.json'); assert not out.exists(), 'Preserve previous run evidence.'
build = Path(__import__('os').environ['LOCALAPPDATA']) / 'ESP32_Companion/build-standalone'
metadata = json.loads((build / 'project_description.json').read_text(encoding='utf-8'))
assert metadata['project_name'] == 'esp32_companion' and metadata['app_bin'] == 'esp32_companion.bin', 'Renamed companion build required.'
image = (build / metadata['app_bin']).read_bytes()
report = {'created_at': utc_now(), 'firmware_sha256': hashlib.sha256(image).hexdigest(), 'status': 'in_progress',
    'live_microphone_capture': False, 'live_audio_uploaded': False, 'raw_audio_saved': False, 'transcript_saved': False,
    'cycles_requested': args.cycles, 'cycles': []}
device = GuardedDevice(); failure = None
try:
    device.connection.open()
    configured, busy = device.protocol(require_capture=True)
    if not configured or busy: raise CheckFailure('Configured idle development robot required.')
    start = device.send(b's'); device.wait(FEATURE.replace('auto_wake=1', 'auto_wake=0'), start)
    device.wait(r'CompanionQuestion: FIXTURE_FEATURE enabled=1 microphone=0', start)
    wait_network(device)
    atomic_json(out, report)
    for index in range(1, args.cycles + 1):
        start = device.send(b'f')
        device.wait(r'CompanionConversation: OWNER mode=standalone heartbeat_required=0', start, timeout=20)
        result = device.wait(RESULT, start, timeout=155)
        device.wait(r'CompanionConversation: UI state=READY wake_armed=1', start, timeout=15)
        text = device.text[start:]
        row = {'cycle': index, 'finished_at': utc_now(), 'ok': bool(int(result[1])), 'http': int(result[2]),
            'reason': result[3], 'upgrade_http': int(result[4]), 'close_ack': int(result[7]), 'close_code': int(result[8]),
            'turns': int(result[9]), 'quiet_discards': int(result[11]), 'elapsed_ms': int(result[12])}
        health = r'UPLOAD_HEALTH turn=(\d+) bytes=(\d+) chunks=(\d+) ended=([01]) upload_ms=(\d+) max_send_ms=(\d+) slow_sends=(\d+)'
        row['uploads'] = [dict(zip(['turn', 'bytes', 'chunks', 'ended', 'upload_ms', 'max_send_ms', 'slow_sends'], map(int, m.groups()))) for m in re.finditer(health, text)]
        pattern = (r'SEND_FAILURE turn=(\d+) opcode=(\d+) expected=(\d+) returned=(-?\d+) elapsed_ms=(\d+) system_errno=(-?\d+) socket_errno=(-?\d+) '
            r'tls_status=(-?\d+) tls_error=(-?\d+) tls_flags=(-?\d+) input_bytes=(\d+) input_chunks=(\d+) wifi=([01]) rssi=(-?\d+) internal_free=(\d+)')
        fields = ['turn', 'opcode', 'expected', 'returned', 'elapsed_ms', 'system_errno', 'socket_errno', 'tls_status', 'tls_error', 'tls_flags', 'input_bytes', 'input_chunks', 'wifi', 'rssi', 'internal_free']
        row['send_failures'] = [dict(zip(fields, map(int, m.groups()))) for m in re.finditer(pattern, text)]
        cleanup = re.search(r'CompanionConversation: CLEANUP reason=([a-z_]+)', text)
        row['cleanup_reason'] = cleanup[1] if cleanup else 'not_reported'
        write_stats = re.search(r'WRITE_STATS calls=(\d+) waits=(\d+) partials=(\d+) question_retries=0', text)
        if write_stats: row['write_stats'] = dict(zip(['calls', 'waits', 'partials'], map(int, write_stats.groups())))
        row['injected_faults'] = re.findall(r'WRITE_FAULT injected=([a-z_]+)', text)
        issue = None
        try:
            if row['ok']:
                metadata = conversation_metadata(text, 2, fixture=True)
                row['played_samples'] = sum(t['played_samples'] for t in metadata['outputs'])
                row['output_clipped'] = sum(t['clipped'] for t in metadata['outputs'])
                assert len(row['uploads']) == 2 and all(u['bytes'] == 128000 and u['chunks'] == 100 and u['ended'] == 1 for u in row['uploads']), 'Incomplete upload metrics.'
            if args.require_recovery:
                assert row['ok'], 'Fault-injected conversation failed.'
                assert row['injected_faults'] == ['header_wait', 'header_short', 'payload_short', 'payload_want_write'], 'Controlled write faults missing.'
                assert row['write_stats']['waits'] >= 2 and row['write_stats']['partials'] >= 2, 'Recovery counters missing.'
            assert 'MicrophonePrep: RECORDING' not in text and text.count('CompanionSession: REQUEST_BEGIN kind=conversation_fixture') == 1, 'Unexpected microphone capture or duplicate request.'
        except (AssertionError, CheckFailure) as error:
            issue = str(error)
        row['validation_failure'] = issue
        report['cycles'].append(row); atomic_json(out, report)
        print(json.dumps(row), flush=True)
        if issue: raise CheckFailure(issue)
        device.text = ''; time.sleep(2)
    report['status'] = 'complete'
except Exception as error:
    failure = str(error) if isinstance(error, (CheckFailure, AssertionError)) else type(error).__name__
    report.update(status='stopped', failure=failure)
finally:
    device.connection.close(); report['closed_at'] = utc_now(); atomic_json(out, report)
print('Saved', out.name, 'completed cycles', len(report['cycles']), 'failure', failure, flush=True)
if failure: raise SystemExit(1)
