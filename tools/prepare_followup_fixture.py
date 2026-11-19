"""Create a public synthetic context-dependent follow-up; no microphone."""
import hashlib
import json
from pathlib import Path
import subprocess
import wave

from prepare_question_fixture import main as prepare_first

ROOT = Path(__file__).resolve().parent.parent
PHRASE = "And what is that number times three?"


def main():
    prepare_first()
    program = r"""
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$followupSynth = New-Object System.Speech.Synthesis.SpeechSynthesizer
try {
    $followupVoice = $followupSynth.GetInstalledVoices() | Where-Object {
        $_.Enabled -and $_.VoiceInfo.Culture.TwoLetterISOLanguageName -eq 'en'
    } | Select-Object -First 1
    if ($null -eq $followupVoice) { throw 'English voice required.' }
    $followupSynth.SelectVoice($followupVoice.VoiceInfo.Name)
    $followupSynth.Rate = 0
    $followupSynth.Volume = 75
    $followupFormat = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo -ArgumentList @(
        16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
        [System.Speech.AudioFormat.AudioChannel]::Mono)
    $followupSynth.SetOutputToWaveFile((Join-Path (Get-Location) 'logs\robot-followup-public-source.wav'), $followupFormat)
    $followupSynth.Speak('And what is that number times three?')
} finally { $followupSynth.Dispose() }
"""
    result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", program], cwd=ROOT, capture_output=True)
    if result.returncode: raise RuntimeError("Windows could not create the public follow-up fixture.")
    with wave.open(str(ROOT / "logs/robot-followup-public-source.wav"), "rb") as source:
        assert (source.getnchannels(), source.getsampwidth(), source.getframerate(), source.getcomptype()) == (1, 2, 16000, "NONE")
        data = bytes(2560) + source.readframes(source.getnframes())
    if not 2560 < len(data) <= 128000 or len(data) % 2: raise RuntimeError("Follow-up phrase does not fit four seconds.")
    data += bytes(128000 - len(data))
    target = ROOT / "logs/robot-followup-public-fixture.pcm"
    target.write_bytes(data)
    metadata = {"source": "Windows System.Speech synthetic fixture", "question": PHRASE, "microphone_opened": False,
        "bytes": len(data), "sample_rate": 16000, "channels": 1, "sample_bits": 16, "sha256": hashlib.sha256(data).hexdigest()}
    target.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print("Prepared synthetic first question and context-dependent follow-up: four seconds each; no microphone.")


if __name__ == "__main__": main()
