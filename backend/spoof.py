"""
spoof.py — Per-profile device-identity spoofing (LSPosed module integration).

This module does NOT implement the actual hooking (that lives inside the
LSPosed module, e.g. an XPrivacyLua/ShellDroid-style companion). Instead it:

  * Builds the per-profile identity payload (IMEI, Android ID, device model,
    etc.) from a validated spec.
  * Writes that payload to a device-side config file that the LSPosed module
    reads on each app start.
  * (Optionally) triggers the module to reload via broadcast or scope toggle.

All identity values are TEST/SANDBOX values generated locally — they never
impersonate a real, third-party device. This is for observing how the target
app reacts to *variations* in the environment, within a lab setting.
"""

from __future__ import annotations

import json
import logging
import random
import string
import time
from typing import Dict, List, Optional

from .adb_helper import AdbHelper
from .config import settings
from .profile_manager import Profile

log = logging.getLogger("qa_harness.spoof")


class SpoofError(RuntimeError):
    """Raised when spoof config generation/application fails."""


# Recognised spoof keys and their generators.
SPOOF_KEYS = {
    "imei": "imei",
    "android_id": "android_id",
    "device_model": "device_model",
    "manufacturer": "manufacturer",
    "serial": "serial",
    "mac": "mac",
}


def _rand_hex(n: int) -> str:
    return "".join(random.choices(string.hexdigits.lower(), k=n))


def _rand_digits(n: int) -> str:
    return "".join(random.choices(string.digits, k=n))


def generate_identity() -> Dict[str, str]:
    """Generate a plausible (but clearly synthetic) test identity.

    These values are designed to be *distinct per profile* and obviously
    non-production (they are random and not tied to any real hardware).
    """
    imei = _rand_digits(14)  # 15th digit is a Luhn checksum, computed below
    imei += _luhn_checksum(imei)
    android_id = _rand_hex(16)  # 64-bit hex, standard format
    model = "QA-Device-" + _rand_digits(4)
    return {
        "imei": imei,
        "android_id": android_id,
        "device_model": model,
        "manufacturer": "QALab",
        "serial": _rand_hex(12),
        "mac": ":".join(_rand_hex(2) for _ in range(6)),
    }


def _luhn_checksum(digits14: str) -> str:
    """Compute the Luhn check digit for a 14-digit IMEI prefix."""
    digits = [int(d) for d in digits14]
    total = 0
    for i, d in enumerate(reversed(digits)):
        if i % 2 == 0:
            d *= 2
            if d > 9:
                d -= 9
        total += d
    check = (10 - (total % 10)) % 10
    return str(check)


def validate_identity(payload: Dict[str, str]) -> Dict[str, str]:
    """Validate/normalize a user-supplied spoof payload.

    Rejects keys we don't manage (to avoid writing junk into the module
    config) and enforces a sane shape. Returns the sanitised dict.
    """
    clean: Dict[str, str] = {}
    for key, value in payload.items():
        if key not in SPOOF_KEYS:
            log.warning("Ignoring unknown spoof key '%s'", key)
            continue
        if not isinstance(value, str) or not value.strip():
            raise SpoofError(f"Spoof value for '{key}' must be a non-empty string")
        clean[key] = value.strip()
    return clean


class SpoofManager:
    """Writes per-profile identity config for the LSPosed module."""

    def __init__(self, adb: AdbHelper) -> None:
        self.adb = adb

    def build_config(self, profile: Profile) -> Dict[str, object]:
        """Produce the device-side JSON config for a profile."""
        identity = profile.spoof or generate_identity()
        return {
            "profile": profile.name,
            "package": settings.target_package,
            "identity": identity,
            "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ"),
        }

    def apply(self, profile: Profile) -> None:
        """Write the profile's identity config to the device staging path."""
        cfg = self.build_config(profile)
        payload = json.dumps(cfg, indent=2)

        # Write via a here-safe mechanism: base64 through `echo`/`cat`.
        # We stage locally, push to /data/local/tmp, then move into place.
        local_tmp = settings.snapshot_root / f"spoof_{profile.name}.json"
        local_tmp.parent.mkdir(parents=True, exist_ok=True)
        local_tmp.write_text(payload, "utf-8")

        staging = f"{settings.device_staging_dir}/spoof_{profile.name}.json"
        self.adb.push(str(local_tmp), staging, check=True)
        # Move into the module's expected config path (root).
        move = (
            f"mkdir -p {settings.device_staging_dir} && "
            f"cp {staging} {settings.spoof_config_path}"
        )
        res = self.adb.shell(move, as_root=True, check=False)
        if not res.ok:
            raise SpoofError(f"Failed to apply spoof config: {res.stderr.strip()}")
        log.info(
            "Applied spoof config for %s -> %s",
            profile.name, settings.spoof_config_path,
        )

    def broadcast_reload(self) -> None:
        """Ask the LSPosed module to re-read its config (best-effort)."""
        # The module should register a receiver for this action. If it does
        # not, the module re-reads config on next app cold start instead.
        action = f"{settings.lsposed_module_pkg}.RELOAD_SPOOF"
        self.adb.shell(
            f"am broadcast -a {action} -p {settings.lsposed_module_pkg}",
            as_root=True, check=False,
        )


def make_spoof_callback(adb: AdbHelper):
    """Return a callback suitable for ``ProfileManager.set_spoof_callback``."""
    mgr = SpoofManager(adb)

    def callback(profile: Profile) -> None:
        mgr.apply(profile)
        mgr.broadcast_reload()

    return callback
