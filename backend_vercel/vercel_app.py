"""
vercel_app.py — Serverless-friendly FastAPI backend for Vercel.

Design:
  * NO `adb`/`subprocess` at import time — import must stay side-effect free
    for Vercel's cold-start bundling.
  * Persistence via Firestore (Firebase Admin SDK) — profiles, messages, logs.
  * ADB commands are NOT executed here (Vercel has no device). Instead they
    are enqueued as documents in the `commands` collection with status
    `pending`; a local worker (worker.py) polls and executes them on the
    machine that has adb + the device attached.

Entrypoint for Vercel: `from backend_vercel.vercel_app import app`
"""

from __future__ import annotations

import datetime as _dt
import json
import logging
import os
import secrets
import uuid
from typing import Any, Dict, List, Optional

from fastapi import Depends, FastAPI, Header, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

logging.basicConfig(level=logging.INFO)
log = logging.getLogger("qa_harness.vercel")

# --------------------------------------------------------------------------- #
# Firebase Admin (lazy init — only when first used, and only if configured)
# --------------------------------------------------------------------------- #
_firestore = None


def _get_firestore():
    """Lazily initialise Firestore from the Firebase Admin SDK.

    Credentials are read from:
      * FIREBASE_CREDENTIALS_JSON  (full service-account JSON in one env var),
      * or FIREBASE_CREDENTIALS_PATH (path to a JSON file).
    If neither is set, returns None and the API runs in "memory-only" mode.
    """
    global _firestore
    if _firestore is not None:
        return _firestore

    creds_raw = os.environ.get("FIREBASE_CREDENTIALS_JSON")
    creds_path = os.environ.get("FIREBASE_CREDENTIALS_PATH")

    if not creds_raw and not creds_path:
        log.warning("Firebase not configured — running in memory-only mode")
        return None

    try:
        import firebase_admin
        from firebase_admin import credentials, firestore

        if creds_raw:
            info = json.loads(creds_raw)
            cred = credentials.Certificate(info)
        else:
            cred = credentials.Certificate(creds_path)

        try:
            firebase_admin.get_app()
        except ValueError:
            firebase_admin.initialize_app(cred)

        _firestore = firestore.client()
        return _firestore
    except Exception as exc:  # noqa: BLE001
        log.error("Firestore init failed: %s", exc)
        return None


# --------------------------------------------------------------------------- #
# In-memory fallback store (used when Firestore is not configured)
# --------------------------------------------------------------------------- #
class MemoryStore:
    """Simple dict-backed store so the API still works without Firebase."""

    def __init__(self) -> None:
        self.profiles: Dict[str, Dict[str, Any]] = {}
        self.commands: Dict[str, Dict[str, Any]] = {}
        self.logs: List[Dict[str, Any]] = []

    def list_profiles(self) -> List[Dict[str, Any]]:
        return [dict(v) for v in self.profiles.values()]

    def upsert_profile(self, name: str, data: Dict[str, Any]) -> None:
        self.profiles[name] = data

    def get_profile(self, name: str) -> Optional[Dict[str, Any]]:
        return self.profiles.get(name)

    def enqueue_command(self, cmd: Dict[str, Any]) -> str:
        cid = uuid.uuid4().hex[:12]
        cmd["id"] = cid
        cmd["status"] = "pending"
        self.commands[cid] = cmd
        return cid

    def list_commands(self, status: Optional[str] = None) -> List[Dict[str, Any]]:
        out = [dict(v) for v in self.commands.values()]
        if status:
            out = [c for c in out if c.get("status") == status]
        return out

    def update_command(self, cid: str, patch: Dict[str, Any]) -> None:
        if cid in self.commands:
            self.commands[cid].update(patch)

    def add_log(self, entry: Dict[str, Any]) -> None:
        self.logs.append(entry)

    def list_logs(self, limit: int = 100) -> List[Dict[str, Any]]:
        return self.logs[-limit:]


_store = MemoryStore()


def _profiles_collection():
    db = _get_firestore()
    return db.collection("profiles") if db else None


def _commands_collection():
    db = _get_firestore()
    return db.collection("commands") if db else None


def _logs_collection():
    db = _get_firestore()
    return db.collection("logs") if db else None


# --------------------------------------------------------------------------- #
# App
# --------------------------------------------------------------------------- #
app = FastAPI(
    title="QA Multi-Profile Harness API",
    description=(
        "Serverless API gateway for the QA multi-profile messaging harness. "
        "Persists profiles/commands/logs to Firestore and enqueues ADB "
        "commands for a local worker. Not for production use."
    ),
    version="1.0.0",
    docs_url="/docs",
    redoc_url="/redoc",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

API_KEY = os.environ.get("QA_API_KEY", "CHANGE_ME_GENERATE_A_RANDOM_KEY")


def require_api_key(x_api_key: Optional[str] = Header(default=None)) -> str:
    if API_KEY == "CHANGE_ME_GENERATE_A_RANDOM_KEY":
        raise HTTPException(500, "QA_API_KEY not configured")
    if not x_api_key or not secrets.compare_digest(x_api_key, API_KEY):
        raise HTTPException(401, "Invalid or missing API key")
    return x_api_key


# --------------------------------------------------------------------------- #
# Models
# --------------------------------------------------------------------------- #
class ProfileCreate(BaseModel):
    name: str = Field(..., description="e.g. 'WA_1'")
    spoof: Optional[Dict[str, str]] = None


class SwitchRequest(BaseModel):
    profile: str
    snapshot_current: bool = True


class TriggerRequest(BaseModel):
    number: str
    message: str
    method: str = "auto"


class BatchRequest(BaseModel):
    items: List[Dict[str, str]]
    delay_min: Optional[float] = None
    delay_max: Optional[float] = None


# --------------------------------------------------------------------------- #
# Endpoints
# --------------------------------------------------------------------------- #
@app.get("/health")
async def health():
    """Unauthenticated liveness probe."""
    return {
        "status": "ok",
        "mode": "firestore" if _get_firestore() else "memory",
        "backend": "vercel",
        "time": _dt.datetime.utcnow().isoformat() + "Z",
    }


@app.get("/profiles", dependencies=[Depends(require_api_key)])
async def list_profiles():
    col = _profiles_collection()
    if col:
        docs = col.stream()
        profiles = [d.to_dict() for d in docs]
        active = next((p["name"] for p in profiles if p.get("status") == "active"), None)
        return {"active": active, "profiles": profiles}
    return {"active": None, "profiles": _store.list_profiles()}


@app.post("/profile", dependencies=[Depends(require_api_key)])
async def create_profile(req: ProfileCreate):
    from backend_vercel.spoof_gen import generate_identity

    name = req.name.strip()
    if not name:
        raise HTTPException(400, "name required")

    data = {
        "name": name,
        "status": "inactive",
        "spoof": req.spoof or generate_identity(),
        "created_at": _dt.datetime.utcnow().isoformat() + "Z",
    }

    col = _profiles_collection()
    if col:
        col.document(name).set(data)
    else:
        _store.upsert_profile(name, data)

    return {"created": name}


@app.post("/profile/switch", dependencies=[Depends(require_api_key)])
async def switch_profile(req: SwitchRequest):
    """Enqueue a switch command for the local worker (no ADB here)."""
    cmd = {
        "type": "switch",
        "profile": req.profile,
        "snapshot_current": req.snapshot_current,
        "created_at": _dt.datetime.utcnow().isoformat() + "Z",
    }
    cid = _enqueue(cmd)
    return {"command_id": cid, "status": "pending", "detail": "queued for local worker"}


@app.post("/message/trigger", dependencies=[Depends(require_api_key)])
async def trigger_message(req: TriggerRequest):
    cmd = {
        "type": "trigger",
        "number": req.number,
        "message": req.message,
        "method": req.method,
        "created_at": _dt.datetime.utcnow().isoformat() + "Z",
    }
    cid = _enqueue(cmd)
    return {"command_id": cid, "status": "pending", "detail": "queued for local worker"}


@app.post("/message/batch", dependencies=[Depends(require_api_key)])
async def batch_message(req: BatchRequest):
    if not req.items:
        raise HTTPException(400, "items must be non-empty")
    cmd = {
        "type": "batch",
        "items": req.items,
        "delay_min": req.delay_min,
        "delay_max": req.delay_max,
        "created_at": _dt.datetime.utcnow().isoformat() + "Z",
    }
    cid = _enqueue(cmd)
    return {"command_id": cid, "status": "pending", "detail": "queued for local worker"}


@app.get("/commands", dependencies=[Depends(require_api_key)])
async def list_commands(status: Optional[str] = None):
    """List queued commands (used by the local worker and the dashboard)."""
    col = _commands_collection()
    if col:
        query = col
        if status:
            query = query.where("status", "==", status)
        docs = query.stream()
        return {"commands": [d.to_dict() for d in docs]}
    return {"commands": _store.list_commands(status)}


@app.get("/logs", dependencies=[Depends(require_api_key)])
async def tail_logs(lines: int = 100):
    col = _logs_collection()
    if col:
        docs = col.order_by("timestamp", direction="DESCENDING").limit(lines).stream()
        return {"lines": [d.to_dict().get("message", "") for d in docs]}
    return {"lines": [e.get("message", "") for e in _store.list_logs(lines)]}


# --------------------------------------------------------------------------- #
# Helpers
# --------------------------------------------------------------------------- #
def _enqueue(cmd: Dict[str, Any]) -> str:
    cmd["status"] = "pending"
    col = _commands_collection()
    if col:
        ref = col.document()
        cmd["id"] = ref.id
        ref.set(cmd)
        return ref.id
    return _store.enqueue_command(cmd)


def _add_log(level: str, message: str, **meta: Any) -> None:
    entry = {
        "timestamp": _dt.datetime.utcnow().isoformat() + "Z",
        "level": level,
        "message": message,
        **meta,
    }
    col = _logs_collection()
    if col:
        col.add(entry)
    else:
        _store.add_log(entry)


@app.get("/")
async def root():
    return {"service": "qa-harness-api", "docs": "/docs", "health": "/health"}
