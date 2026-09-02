"""
app.py — FastAPI backend for the QA Multi-Profile Harness.

Endpoints
---------
  POST /profile/switch      -> switch active profile (WA_1 <-> WA_2 ...)
  POST /profile             -> provision a new profile
  GET  /profiles            -> list all profiles + status
  POST /message/trigger     -> send a single test message
  POST /message/batch       -> run a sequence of triggers (JSON body)
  POST /message/batch/upload-> upload CSV/TXT and run a batch
  GET  /logs                -> tail recent structured log lines
  GET  /health              -> liveness/readiness probe

Auth: every mutating endpoint requires the ``X-API-Key`` header to match
``settings.api_key``. A helper ``GET /health`` is unauthenticated.

Run:  uvicorn backend.app:app --host 0.0.0.0 --port 8000
"""

from __future__ import annotations

import asyncio
import logging
import secrets
import uuid
from typing import List, Optional

from fastapi import Depends, FastAPI, File, Header, HTTPException, UploadFile
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

from .adb_helper import AdbHelper, AdbError
from .config import settings
from .logging_setup import set_request_id, setup_logging
from .messaging import MessageEngine, MessageError, parse_batch_file
from .profile_manager import Profile, ProfileError, ProfileManager
from .spoof import make_spoof_callback, validate_identity, generate_identity, SpoofError

log = logging.getLogger("qa_harness.app")

setup_logging()

app = FastAPI(
    title="QA Multi-Profile Harness",
    description=(
        "Internal QA/DevOps backend for isolated multi-profile testing of "
        "a messaging app on a rooted Android device. Not for production use."
    ),
    version="1.0.0",
    docs_url="/docs",
    redoc_url="/redoc",
)

# --- CORS (allow the Flutter web dashboard to reach this API) -------------
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # lab network; tighten in production
    allow_methods=["*"],
    allow_headers=["*"],
)


# --- Request-id middleware -------------------------------------------------
@app.middleware("http")
async def request_context(request, call_next):
    rid = request.headers.get("X-Request-ID") or uuid.uuid4().hex[:12]
    set_request_id(rid)
    response = await call_next(request)
    response.headers["X-Request-ID"] = rid
    return response


# --- Device-layer exception translation --------------------------------------
# ADB / spoof / message-engine failures are environmental (no device, no root,
# etc.), not client mistakes — surface them as 503 with a clear message instead
# of an opaque 500.
@app.exception_handler(AdbError)
async def _adb_error_handler(request, exc: AdbError):
    log.warning("ADB error on %s: %s", request.url.path, exc)
    return JSONResponse(status_code=503, content={"detail": str(exc)})


@app.exception_handler(SpoofError)
async def _spoof_error_handler(request, exc: SpoofError):
    log.warning("Spoof error on %s: %s", request.url.path, exc)
    return JSONResponse(status_code=503, content={"detail": str(exc)})


@app.exception_handler(MessageError)
async def _message_error_handler(request, exc: MessageError):
    log.warning("Message error on %s: %s", request.url.path, exc)
    return JSONResponse(status_code=400, content={"detail": str(exc)})


# --- Wiring ----------------------------------------------------------------
adb = AdbHelper()
profiles = ProfileManager(adb)
profiles.set_spoof_callback(make_spoof_callback(adb))
messaging = MessageEngine(adb, profiles)


# --- Auth -------------------------------------------------------------------
def require_api_key(x_api_key: Optional[str] = Header(default=None)) -> str:
    if settings.api_key == "CHANGE_ME_GENERATE_A_RANDOM_KEY":
        raise HTTPException(
            status_code=500,
            detail="API key not configured. Set QA_API_KEY env var.",
        )
    if not x_api_key or not secrets.compare_digest(x_api_key, settings.api_key):
        raise HTTPException(status_code=401, detail="Invalid or missing API key")
    return x_api_key


# --- Request models ----------------------------------------------------------
class SwitchRequest(BaseModel):
    profile: str = Field(..., description="Target profile name, e.g. 'WA_2'")
    snapshot_current: bool = Field(True, description="Snapshot current profile before switching")


class CreateProfileRequest(BaseModel):
    name: str = Field(..., description="Profile name, e.g. 'WA_3'")
    spoof: Optional[dict] = Field(None, description="Optional identity overrides")


class TriggerRequest(BaseModel):
    number: str = Field(..., description="Destination test number")
    message: str = Field(..., description="Message body")
    method: str = Field("auto", description="auto | intent | typing")


class BatchRequest(BaseModel):
    items: List[dict] = Field(..., description="List of {number, message}")
    delay_min: Optional[float] = Field(None, ge=0, description="Min delay seconds")
    delay_max: Optional[float] = Field(None, ge=0, description="Max delay seconds")


# --- Endpoints ---------------------------------------------------------------
def _safe_device_state() -> dict:
    """Introspect device state without crashing when adb/device is absent."""
    try:
        adb_state = adb.get_state()
    except AdbError:
        adb_state = "unavailable"
    try:
        rooted = adb.is_rooted()
    except AdbError:
        rooted = False
    return {"adb_state": adb_state, "rooted": rooted}


@app.get("/health")
async def health():
    """Liveness probe (unauthenticated)."""
    dev = _safe_device_state()
    return {
        "status": "ok",
        "adb_state": dev["adb_state"],
        "rooted": dev["rooted"],
        "profiles": len(profiles.list_profiles()),
        "active": profiles.current().name if profiles.current() else None,
    }


@app.get("/profiles", dependencies=[Depends(require_api_key)])
async def list_profiles():
    """List all profiles with active/inactive status."""
    return {
        "active": profiles.current().name if profiles.current() else None,
        "profiles": [
            {
                "name": p.name,
                "android_user": p.android_user,
                "status": p.status,
                "spoof": p.spoof,
                "snapshot_path": p.snapshot_path,
                "created_at": p.created_at,
            }
            for p in profiles.list_profiles()
        ],
    }


@app.post("/profile", dependencies=[Depends(require_api_key)])
async def create_profile(req: CreateProfileRequest):
    """Provision a new profile (up to settings.max_profiles)."""
    try:
        spoof = validate_identity(req.spoof) if req.spoof else None
        profile = await asyncio.to_thread(
            profiles.create_profile, req.name, spoof or generate_identity()
        )
        return {"created": profile.name, "android_user": profile.android_user}
    except ProfileError as exc:
        raise HTTPException(status_code=400, detail=str(exc))


@app.post("/profile/switch", dependencies=[Depends(require_api_key)])
async def switch_profile(req: SwitchRequest):
    """Switch the active profile (simulated tap/ADB + user switch + snapshot)."""
    try:
        profile = await asyncio.to_thread(
            profiles.switch, req.profile, snapshot_current=req.snapshot_current
        )
        return {"active": profile.name, "android_user": profile.android_user}
    except ProfileError as exc:
        raise HTTPException(status_code=400, detail=str(exc))


@app.post("/message/trigger", dependencies=[Depends(require_api_key)])
async def trigger_message(req: TriggerRequest):
    """Trigger a single test message to a number."""
    res = await asyncio.to_thread(
        messaging.trigger, req.number, req.message, method=req.method
    )
    return {"ok": res.ok, "number": res.number, "elapsed_ms": res.elapsed_ms,
            "error": res.error}


@app.post("/message/batch", dependencies=[Depends(require_api_key)])
async def batch_message(req: BatchRequest):
    """Run a sequence of triggers with jittered 5–10 s delays."""
    if not req.items:
        raise HTTPException(status_code=400, detail="items must be non-empty")
    result = await messaging.batch(
        req.items, delay_min=req.delay_min, delay_max=req.delay_max
    )
    return {
        "total": result.total,
        "succeeded": result.succeeded,
        "failed": result.failed,
        "results": [
            {"index": r.index, "number": r.number, "ok": r.ok, "error": r.error}
            for r in result.results
        ],
    }


@app.post("/message/batch/upload", dependencies=[Depends(require_api_key)])
async def batch_upload(
    file: UploadFile = File(...),
    delay_min: Optional[float] = None,
    delay_max: Optional[float] = None,
):
    """Upload a CSV/TXT file and run it as a batch.

    CSV: ``number,message`` per line (header optional).
    TXT: ``number<TAB>message`` or ``number,message`` per line.
    """
    content = await file.read()
    items = parse_batch_file(content, file.filename or "upload.txt")
    if not items:
        raise HTTPException(status_code=400, detail="No valid rows parsed")
    result = await messaging.batch(items, delay_min=delay_min, delay_max=delay_max)
    return {
        "filename": file.filename,
        "parsed": len(items),
        "total": result.total,
        "succeeded": result.succeeded,
        "failed": result.failed,
    }


@app.get("/logs", dependencies=[Depends(require_api_key)])
async def tail_logs(lines: int = 100):
    """Return the most recent structured log lines (for the dashboard)."""
    import glob

    log_files = sorted(glob.glob(str(settings.log_dir / "harness-*.jsonl")))
    if not log_files:
        return {"lines": []}
    latest = log_files[-1]
    with open(latest, "r", encoding="utf-8") as fh:
        all_lines = fh.readlines()
    tail = all_lines[-lines:]
    return {"file": latest, "lines": [ln.rstrip("\n") for ln in tail]}


# --- Convenience: auto-generate a key if none set ---------------------------
def _print_startup_banner() -> None:
    if settings.api_key == "CHANGE_ME_GENERATE_A_RANDOM_KEY":
        generated = secrets.token_urlsafe(32)
        settings.api_key = generated
        log.warning(
            "No API key configured — generated one for this run: %s "
            "(set QA_API_KEY to make it stable)", generated,
        )


_print_startup_banner()
