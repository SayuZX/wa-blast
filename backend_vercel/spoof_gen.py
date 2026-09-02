"""spoof_gen.py — self-contained identity generator (no ADB dependency).

Kept separate from the local backend's spoof.py so the Vercel bundle stays
lean and import-safe.
"""

from __future__ import annotations

import random
import string
from typing import Dict


def _rand_hex(n: int) -> str:
    return "".join(random.choices(string.hexdigits.lower(), k=n))


def _rand_digits(n: int) -> str:
    return "".join(random.choices(string.digits, k=n))


def _luhn_checksum(digits14: str) -> str:
    digits = [int(d) for d in digits14]
    total = 0
    for i, d in enumerate(reversed(digits)):
        if i % 2 == 0:
            d *= 2
            if d > 9:
                d -= 9
        total += d
    return str((10 - (total % 10)) % 10)


def generate_identity() -> Dict[str, str]:
    """Generate a synthetic (clearly non-production) test identity."""
    imei14 = _rand_digits(14)
    imei = imei14 + _luhn_checksum(imei14)
    return {
        "imei": imei,
        "android_id": _rand_hex(16),
        "device_model": "QA-Device-" + _rand_digits(4),
        "manufacturer": "QALab",
        "serial": _rand_hex(12),
        "mac": ":".join(_rand_hex(2) for _ in range(6)),
    }
