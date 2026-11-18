"""Create the fixed synthetic question locally; never open a microphone."""

from array import array
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import wave


QUESTION = "What is two plus two?"
ROOT = Path(__file__).resolve().parent.parent
LOGS = ROOT / "logs"


def main():
    if os.name != "nt":
        raise RuntimeError("This fixture uses Windows System.Speech and an installed English voice.")
    LOGS.mkdir(exist_ok=True)
    # Constant PowerShell program: no user content, credentials or computed
    # shell commands. Speech is routed to a file, not the audio input/output.
    program = r"""
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$fixtureSynth = New-Object System.Speech.Synthesis.SpeechSynthesizer
try {
    $fixtureVoice = $fixtureSynth.GetInstalledVoices() | Where-Object {
        $_.Enabled -and $_.VoiceInfo.Culture.TwoLetterISOLanguageName -eq 'en'
    } | Select-Object -First 1
    if ($null -eq $fixtureVoice) { throw 'An English speech voice is required.' }
    $fixtureSynth.SelectVoice($fixtureVoice.VoiceInfo.Name)
    $fixtureSynth.Rate = 0
    $fixtureSynth.Volume = 75
    $fixtureFormat = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo -ArgumentList @(
        16000,
        [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
        [System.Speech.AudioFormat.AudioChannel]::Mono
    )
    $fixturePath = Join-Path (Get-Location) 'logs\gemini-input-question-source.wav'
    $fixtureSynth.SetOutputToWaveFile($fixturePath, $fixtureFormat)
    $fixtureSynth.Speak('What is two plus two?')
} finally {
    $fixtureSynth.Dispose()
}
"""
    result = subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", program],
        cwd=ROOT, capture_output=True, text=True,
    )
    if result.returncode:
        raise RuntimeError("Windows could not synthesize the fixed English question.")
    with wave.open(str(LOGS / "gemini-input-question-source.wav"), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate(), source.getcomptype()) != (1, 2, 16000, "NONE"):
            raise RuntimeError("Synthetic source has an unexpected PCM format.")
        original = source.readframes(source.getnframes())
    # Preserve the complete utterance, with 80 ms before and 320 ms after it.
    pcm = bytes(16000 * 2 * 80 // 1000) + original + bytes(16000 * 2 * 320 // 1000)
    if not pcm or len(pcm) % 2 or len(pcm) > 16000 * 2 * 6:
        raise RuntimeError("Synthetic question exceeds the six-second input bound.")
    samples = array("h", pcm)
    if sys.byteorder != "little":
        samples.byteswap()
    peak = max(abs(sample) for sample in samples)
    rms = math.sqrt(sum(sample * sample for sample in samples) / len(samples))
    if peak == 0 or rms == 0:
        raise RuntimeError("Synthetic question is silent.")
    (LOGS / "gemini-input-question.pcm").write_bytes(pcm)
    with wave.open(str(LOGS / "gemini-input-question.wav"), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(16000)
        output.writeframes(pcm)
    metadata = {
        "source": "Windows System.Speech synthetic fixture",
        "question": QUESTION,
        "sample_rate": 16000,
        "channels": 1,
        "sample_bits": 16,
        "bytes": len(pcm),
        "seconds": len(pcm) / 32000,
        "sha256": hashlib.sha256(pcm).hexdigest(),
        "peak": peak,
        "rms": round(rms, 2),
        "clipped_samples": sum(sample in {-32768, 32767} for sample in samples),
        "microphone_opened": False,
    }
    (LOGS / "gemini-input-question.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(f"PASS fixed synthetic question: {metadata['seconds']:.3f}s, 16 kHz mono PCM; no microphone opened.")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, wave.Error) as error:
        raise SystemExit(f"Fixture preparation failed: {error}") from None
