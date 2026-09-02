"""
profile_manager.py — Hybrid multi-profile management for the QA harness.

Strategy (hybrid, per your spec):
  1. **Android multi-user** — each logical profile maps to an Android user
     (``user 0`` = "Owner/WA_1", ``user 10/11/...`` = WA_2..WA_N). This gives
     clean process & data isolation at the OS level.
  2. **Data snapshot** — within/between users, we snapshot and restore the
     target package's private data (``/data/data/<pkg>`` and, optionally,
     ``/data/media/<user>/Android/...``) using tar under the root shell.
     Snapshots live on the HOST under ``settings.snapshot_root``.

This module is intentionally synchronous (the profile switch is a blocking,
order-sensitive operation). The FastAPI layer wraps calls in ``asyncio.to_thread``
to avoid blocking the event loop.

The mapping between logical names (WA_1..WA_N) and Android user ids is stored
in a JSON registry on the host so it survives backend restarts.
"""

from __future__ import annotations

import json
import logging
import shutil
import time
import uuid
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

from .adb_helper import AdbHelper, AdbError
from .config import settings

log = logging.getLogger("qa_harness.profile")

REGISTRY_FILE = settings.snapshot_root / "registry.json"

# Android user ids: 0 is always Owner; secondary users start at 10.
FIRST_SECONDARY_USER = 10


class ProfileError(RuntimeError):
    """Raised on any profile operation failure."""


@dataclass
class Profile:
    name: str                     # logical name, e.g. "WA_1"
    android_user: int             # Android user id, e.g. 0 or 10
    active: bool = False
    spoof: Dict[str, str] = field(default_factory=dict)  # imei/android_id/model...
    created_at: str = field(default_factory=lambda: time.strftime("%Y-%m-%dT%H:%M:%SZ"))
    snapshot_path: Optional[str] = None

    @property
    def display_name(self) -> str:
        return self.name

    @property
    def status(self) -> str:
        return "active" if self.active else "inactive"


class ProfileManager:
    def __init__(self, adb: AdbHelper, registry_file: Optional[Path] = None) -> None:
        self.adb = adb
        self.registry_file = registry_file or REGISTRY_FILE
        self._profiles: Dict[str, Profile] = {}
        self._load_registry()

    # ------------------------------------------------------------------ #
    # Registry persistence
    # ------------------------------------------------------------------ #
    def _load_registry(self) -> None:
        if not self.registry_file.exists():
            self._profiles = {}
            return
        try:
            raw = json.loads(self.registry_file.read_text("utf-8"))
            self._profiles = {
                k: Profile(**v) for k, v in raw.get("profiles", {}).items()
            }
        except (json.JSONDecodeError, TypeError) as exc:
            log.warning("Could not parse registry %s: %s", self.registry_file, exc)
            self._profiles = {}

    def _save_registry(self) -> None:
        self.registry_file.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "profiles": {k: asdict(v) for k, v in self._profiles.items()},
            "updated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ"),
        }
        self.registry_file.write_text(
            json.dumps(payload, indent=2, ensure_ascii=False), "utf-8"
        )

    # ------------------------------------------------------------------ #
    # Queries
    # ------------------------------------------------------------------ #
    def list_profiles(self) -> List[Profile]:
        return sorted(self._profiles.values(), key=lambda p: p.name)

    def get(self, name: str) -> Optional[Profile]:
        return self._profiles.get(name)

    def current(self) -> Optional[Profile]:
        for p in self._profiles.values():
            if p.active:
                return p
        return None

    # ------------------------------------------------------------------ #
    # Provisioning (create profiles up to settings.max_profiles)
    # ------------------------------------------------------------------ #
    def create_profile(self, name: str, spoof: Optional[Dict[str, str]] = None) -> Profile:
        if name in self._profiles:
            raise ProfileError(f"Profile '{name}' already exists")
        if len(self._profiles) >= settings.max_profiles:
            raise ProfileError(
                f"Profile limit reached ({settings.max_profiles}); "
                "cannot create more profiles"
            )

        spoof = spoof or {}
        # WA_1 is the Owner user (0); subsequent profiles get fresh users.
        if not self._profiles:
            android_user = 0
        else:
            android_user = self._next_free_user_id()
            self._create_android_user(android_user)

        profile = Profile(
            name=name,
            android_user=android_user,
            active=False,
            spoof=spoof,
        )
        self._profiles[name] = profile
        self._save_registry()
        log.info("Created profile %s -> android user %d", name, android_user)
        return profile

    def _next_free_user_id(self) -> int:
        used = {p.android_user for p in self._profiles.values()}
        uid = FIRST_SECONDARY_USER
        while uid in used:
            uid += 1
        return uid

    def _create_android_user(self, user_id: int) -> None:
        """Create a secondary Android user (requires root)."""
        res = self.adb.shell(f"pm create-user --profileOf 0 --managed QA_{user_id}", as_root=True)
        # Some ROMs return the new user id on success; others just 0.
        if not res.ok and "already exists" not in res.stderr.lower():
            log.warning(
                "create-user for %d returned rc=%s (may already exist): %s",
                user_id, res.returncode, res.stderr.strip(),
            )
        time.sleep(1.0)

    # ------------------------------------------------------------------ #
    # Snapshot / restore (Titanium-Backup-style data isolation)
    # ------------------------------------------------------------------ #
    def _snapshot_name(self, profile_name: str) -> str:
        safe = profile_name.replace("/", "_")
        return f"{safe}__{uuid.uuid4().hex[:8]}"

    def _device_data_dir(self, user_id: int) -> str:
        """Path to the target app's private data for a given Android user."""
        if user_id == 0:
            return f"/data/data/{settings.target_package}"
        return f"/data/user/{user_id}/{settings.target_package}"

    def snapshot(self, profile_name: str) -> str:
        """Capture the current data of the active profile to host storage."""
        profile = self.get(profile_name)
        if not profile:
            raise ProfileError(f"Unknown profile '{profile_name}'")
        if not profile.active:
            raise ProfileError(
                f"Profile '{profile_name}' is not active; switch to it before snapshotting"
            )

        snap_name = self._snapshot_name(profile_name)
        staging = f"{settings.device_staging_dir}/{snap_name}.tar.gz"
        data_dir = self._device_data_dir(profile.android_user)

        log.info("Snapshotting %s (%s) -> %s", profile_name, data_dir, snap_name)
        # tar on-device (root can read the private dir), then pull to host.
        tar_cmd = (
            f"mkdir -p {settings.device_staging_dir} && "
            f"tar -czf {staging} -C {data_dir} ."
        )
        res = self.adb.shell(tar_cmd, as_root=True, check=False)
        if not res.ok:
            raise ProfileError(
                f"Snapshot tar failed for '{profile_name}': {res.stderr.strip()}"
            )

        dest_dir = settings.snapshot_root / profile_name
        dest_dir.mkdir(parents=True, exist_ok=True)
        dest_file = dest_dir / f"{snap_name}.tar.gz"
        self.adb.pull(staging, str(dest_file), check=True)
        # Clean up device staging file.
        self.adb.shell(f"rm -f {staging}", as_root=True, check=False)

        profile.snapshot_path = str(dest_file)
        self._save_registry()
        log.info("Snapshot saved: %s", dest_file)
        return str(dest_file)

    def restore(self, profile_name: str, snapshot_path: Optional[str] = None) -> None:
        """Restore a profile's data from a snapshot.

        If ``snapshot_path`` is omitted, uses the profile's most recent
        snapshot file on disk.
        """
        profile = self.get(profile_name)
        if not profile:
            raise ProfileError(f"Unknown profile '{profile_name}'")

        path = snapshot_path or profile.snapshot_path
        if not path:
            # Fall back to latest snapshot in the profile's dir.
            snap_dir = settings.snapshot_root / profile_name
            files = sorted(snap_dir.glob("*.tar.gz"), key=lambda p: p.stat().st_mtime, reverse=True)
            if not files:
                raise ProfileError(f"No snapshot available for '{profile_name}'")
            path = str(files[0])

        if not Path(path).exists():
            raise ProfileError(f"Snapshot file not found: {path}")

        staging = f"{settings.device_staging_dir}/restore_{profile.android_user}.tar.gz"
        data_dir = self._device_data_dir(profile.android_user)

        log.info("Restoring %s from %s", profile_name, path)
        self.adb.push(path, staging, check=True)

        # Wipe current data, then untar.
        wipe = f"rm -rf {data_dir}/* {data_dir}/.[!.]* 2>/dev/null; mkdir -p {data_dir}"
        self.adb.shell(wipe, as_root=True, check=False)
        untar = f"tar -xzf {staging} -C {data_dir}"
        res = self.adb.shell(untar, as_root=True, check=False)
        if not res.ok:
            raise ProfileError(f"Restore untar failed: {res.stderr.strip()}")
        self.adb.shell(f"rm -f {staging}", as_root=True, check=False)

        # Fix ownership (app runs as its own uid, not root).
        self.adb.shell(
            f"chown -R $(stat -c %u {data_dir} 2>/dev/null || echo 0):"
            f"$(stat -c %g {data_dir} 2>/dev/null || echo 0) {data_dir}",
            as_root=True, check=False,
        )
        log.info("Restore complete for %s", profile_name)

    # ------------------------------------------------------------------ #
    # Switching
    # ------------------------------------------------------------------ #
    def switch(self, target_name: str, *, snapshot_current: bool = True) -> Profile:
        """Switch the active profile to ``target_name``.

        Order of operations:
          1. Snapshot current profile (if requested and one is active).
          2. Apply spoof config for the target (LSPosed module).
          3. Switch Android user via ``am switch-user``.
          4. Restore target profile's data snapshot (if present).
          5. Relaunch the target app.
        """
        target = self.get(target_name)
        if not target:
            raise ProfileError(f"Unknown profile '{target_name}'")

        current = self.current()
        if current and current.name == target_name:
            log.info("Profile %s is already active", target_name)
            return target

        # 1. Snapshot current profile's data so we can restore later.
        if current and snapshot_current:
            try:
                self.snapshot(current.name)
            except ProfileError as exc:
                log.warning("Snapshot of current profile failed (continuing): %s", exc)

        # 2. Spoof config (delegated to spoof module via callback).
        if self._spoof_callback is not None:
            self._spoof_callback(target)

        # 3. Switch Android user.
        if target.android_user != 0:
            res = self.adb.shell(f"am switch-user {target.android_user}", as_root=True, check=False)
            if not res.ok:
                raise ProfileError(
                    f"switch-user {target.android_user} failed: {res.stderr.strip()}"
                )
            time.sleep(2.0)
        else:
            # Back to owner.
            self.adb.shell("am switch-user 0", as_root=True, check=False)
            time.sleep(2.0)

        # 4. Restore data snapshot if we have one.
        if target.snapshot_path:
            self.restore(target_name, target.snapshot_path)

        # 5. Relaunch the app in the target user.
        self.adb.force_stop(settings.target_package)
        time.sleep(0.5)
        self.adb.start_activity(settings.target_package, settings.target_activity)

        # Mark active state.
        for p in self._profiles.values():
            p.active = (p.name == target_name)
        self._save_registry()
        log.info("Switched active profile -> %s", target_name)
        return target

    # Spoof callback injection (avoids a hard import cycle with spoof.py).
    _spoof_callback = None

    def set_spoof_callback(self, cb) -> None:
        """Register a callable(profile) invoked before each user switch."""
        self._spoof_callback = cb
