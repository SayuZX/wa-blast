"""
config.py — Central configuration for the QA Multi-Profile Harness (backend).

All tunables live here so the rest of the codebase stays declarative and
easy to audit. Values can be overridden via environment variables (prefixed
with ``QA_``) or a local ``config.yaml`` (optional, lower priority than env).

This module is intentionally dependency-free (stdlib only) so it can be
imported by every other backend module without import cycles.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional


def _env(name: str, default: str) -> str:
    """Read ``QA_<NAME>`` from the environment, falling back to ``default``."""
    return os.environ.get(f"QA_{name}", default)


def _env_bool(name: str, default: bool) -> bool:
    raw = _env(name, str(default)).strip().lower()
    return raw in {"1", "true", "yes", "on"}


def _env_int(name: str, default: int) -> int:
    try:
        return int(_env(name, str(default)))
    except (TypeError, ValueError):
        return default


def _env_float(name: str, default: float) -> float:
    try:
        return float(_env(name, str(default)))
    except (TypeError, ValueError):
        return default


@dataclass
class Settings:
    # --- Network / server -------------------------------------------------
    host: str = _env("HOST", "0.0.0.0")
    port: int = _env_int("PORT", 8000)

    # --- Authentication ----------------------------------------------------
    # API key used to authenticate every mutating request via the
    # ``X-API-Key`` header. Override with QA_API_KEY (or generate one at
    # first run if left as the placeholder below).
    api_key: str = _env("API_KEY", "CHANGE_ME_GENERATE_A_RANDOM_KEY")

    # --- Android / ADB -----------------------------------------------------
    # The serial of the target device. Empty string => use the single
    # connected device (or ``$ANDROID_SERIAL``).
    adb_serial: str = _env("ADB_SERIAL", "")
    # Path to the adb binary. Usually on PATH, but you can pin it.
    adb_path: str = _env("ADB_PATH", "adb")
    # Package name of the app under test (e.g. WhatsApp, Telegram, custom).
    target_package: str = _env("TARGET_PACKAGE", "com.whatsapp")
    # Main launch activity, used for cold-start / relaunch during switching.
    target_activity: str = _env(
        "TARGET_ACTIVITY", "com.whatsapp.Main"
    )

    # --- Profile management ------------------------------------------------
    # Where per-profile data snapshots are stored on the HOST (the machine
    # running this backend). Each profile gets a subdirectory here.
    snapshot_root: Path = Path(_env("SNAPSHOT_ROOT", "./data/profiles"))
    # Maximum number of profiles supported (spec: 1–100).
    max_profiles: int = _env_int("MAX_PROFILES", 100)
    # Device-side staging dir for tar snapshots (must be readable by adb
    # shell under the rooted shell user).
    device_staging_dir: str = _env("DEVICE_STAGING_DIR", "/data/local/tmp/qa_harness")

    # --- Spoofing / LSPosed ------------------------------------------------
    # LSPosed module package that implements the per-profile identity spoof.
    lsposed_module_pkg: str = _env("LSPOSED_MODULE_PKG", "com.example.spoofmodule")
    # Path (device-side) to the module's runtime config that we rewrite on
    # profile switch. XPrivacyLua-style modules read a JSON/ini config.
    spoof_config_path: str = _env("SPOOF_CONFIG_PATH", "/data/local/tmp/qa_harness/spoof.json")

    # --- Message trigger ----------------------------------------------------
    # Default inter-message delay for /message/batch (seconds).
    batch_delay_min: float = _env_float("BATCH_DELAY_MIN", 5.0)
    batch_delay_max: float = _env_float("BATCH_DELAY_MAX", 10.0)

    # --- Logging ------------------------------------------------------------
    log_level: str = _env("LOG_LEVEL", "INFO")
    log_dir: Path = Path(_env("LOG_DIR", "./data/logs"))
    # Structured (JSON lines) logs in addition to human-readable console.
    json_logging: bool = _env_bool("JSON_LOGGING", True)

    def ensure_dirs(self) -> None:
        """Create runtime directories if missing."""
        for d in (self.snapshot_root, self.log_dir):
            d.mkdir(parents=True, exist_ok=True)


# A module-level singleton used across the app.
settings = Settings()


def load_settings() -> Settings:
    """Re-read environment and return a fresh Settings (used in tests)."""
    settings.ensure_dirs()
    return settings
