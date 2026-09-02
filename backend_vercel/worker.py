"""worker.py — Local worker that polls the `commands` collection and executes
ADB commands on a machine that actually has adb + the device attached.

This is the piece that bridges the serverless API (Vercel) to the rooted
Android device. Run it on your dev machine / lab server:

    python -m backend_vercel.worker

Environment (same as the Vercel app + local adb config):
    FIREBASE_CREDENTIALS_JSON / FIREBASE_CREDENTIALS_PATH  (Firestore access)
    QA_ADB_SERIAL / QA_ADB_PATH / QA_TARGET_PACKAGE / QA_TARGET_ACTIVITY
"""

from __future__ import annotations

import datetime
import json
import logging
import os
import sys
import time
from typing import Any, Dict, Optional

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("qa_harness.worker")

POLL_INTERVAL = float(os.environ.get("QA_WORKER_POLL", "3.0"))


def _get_firestore():
    import firebase_admin
    from firebase_admin import credentials, firestore

    creds_raw = os.environ.get("FIREBASE_CREDENTIALS_JSON")
    creds_path = os.environ.get("FIREBASE_CREDENTIALS_PATH")
    if not creds_raw and not creds_path:
        log.error("Firebase not configured; set FIREBASE_CREDENTIALS_JSON or _PATH")
        sys.exit(1)
    cred = (
        credentials.Certificate(json.loads(creds_raw))
        if creds_raw
        else credentials.Certificate(creds_path)
    )
    try:
        firebase_admin.get_app()
    except ValueError:
        firebase_admin.initialize_app(cred)
    return firestore.client()


def execute_adb(command: Dict[str, Any]) -> Dict[str, Any]:
    """Execute an ADB command locally. Returns a result dict."""
    from backend.adb_helper import AdbHelper
    from backend.config import settings

    adb = AdbHelper(serial=settings.adb_serial or "", adb_path=settings.adb_path)
    ctype = command.get("type")
    result: Dict[str, Any] = {"ok": False, "error": None}

    try:
        if ctype == "switch":
            # Reuse the local profile manager for a full switch (snapshot etc.).
            from backend.profile_manager import ProfileManager
            from backend.spoof import make_spoof_callback

            pm = ProfileManager(adb)
            pm.set_spoof_callback(make_spoof_callback(adb))
            target = command.get("profile")
            pm.switch(target, snapshot_current=command.get("snapshot_current", True))
            result = {"ok": True, "active": target}
        elif ctype == "trigger":
            number = command.get("number", "")
            message = command.get("message", "")
            method = command.get("method", "auto")
            # Simple intent-based trigger.
            uri = f"https://wa.me/{number}?text={message}"
            r = adb.send_view_intent(uri)
            result = {"ok": r.ok, "error": r.stderr.strip() if not r.ok else None}
        elif ctype == "batch":
            items = command.get("items", [])
            delay_min = command.get("delay_min") or 5.0
            delay_max = command.get("delay_max") or 10.0
            results = []
            for i, item in enumerate(items):
                uri = f"https://wa.me/{item.get('number','')}?text={item.get('message','')}"
                r = adb.send_view_intent(uri)
                results.append({"index": i, "ok": r.ok})
                if i < len(items) - 1:
                    time.sleep((delay_min + delay_max) / 2)
            result = {"ok": True, "results": results}
        else:
            result = {"ok": False, "error": f"unknown command type {ctype}"}
    except Exception as exc:  # noqa: BLE001
        log.exception("ADB execution failed")
        result = {"ok": False, "error": str(exc)}

    return result


def main() -> None:
    db = _get_firestore()
    commands_ref = db.collection("commands")
    logs_ref = db.collection("logs")
    log.info("Worker started. Polling every %.1fs", POLL_INTERVAL)

    while True:
        try:
            # Fetch pending commands.
            pending = commands_ref.where("status", "==", "pending").stream()
            for doc in pending:
                cmd = doc.to_dict()
                cid = doc.id
                log.info("Processing command %s (type=%s)", cid, cmd.get("type"))
                # Mark in-progress.
                commands_ref.document(cid).update({"status": "in_progress"})
                result = execute_adb(cmd)
                commands_ref.document(cid).update(
                    {"status": "done" if result["ok"] else "failed", "result": result}
                )
                logs_ref.add(
                    {
                        "timestamp": datetime.datetime.utcnow().isoformat() + "Z",
                        "level": "info" if result["ok"] else "error",
                        "message": f"command {cid} ({cmd.get('type')}) -> "
                        + ("done" if result["ok"] else f"failed: {result.get('error')}"),
                    }
                )
        except Exception:  # noqa: BLE001
            log.exception("Poll cycle error")

        time.sleep(POLL_INTERVAL)


if __name__ == "__main__":
    main()
