"""Deterministic synthetic acoustic profiles; never substitutes for room audio."""
import numpy as np

RATE = 16000
PROFILES = ['clean', 'quiet', 'noise_20db', 'noise_10db', 'echo', 'playback']


def audio_profile(sound, profile, rng):
    sound = sound.astype(np.float64)
    leading, trailing = int(.6 * RATE), int(1.2 * RATE)
    result = np.pad(sound, (leading, trailing))
    rms = float(np.sqrt(np.mean(sound ** 2) + 1))
    if profile == 'quiet':
        result *= .2
    elif profile.startswith('noise_'):
        snr = int(profile.split('_')[1][:-2])
        result += rng.normal(0, rms * 10 ** (-snr / 20), len(result))
    elif profile == 'echo':
        delay = int(.08 * RATE)
        result[delay:] += .35 * result[:-delay].copy()
    elif profile == 'playback':
        result = np.convolve(result, np.ones(5) / 5, mode='same')
        result *= .7
    elif profile != 'clean':
        raise ValueError('Unknown acoustic profile: ' + profile)
    return np.clip(result, -32768, 32767).astype('<i2')
