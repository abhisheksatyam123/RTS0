#!/usr/bin/env python3
"""Two-client host/join playtest automation for RTS0 (docs/PLAN.md P1 REAL gate).

Drives two REAL Godot clients: one hosts, one joins, over the in-game
multiplayer HUD, then holds the session for N minutes and scans both logs
for out-of-sync / hash-mismatch / disconnect / parse evidence.

    python3 tools/playtest_two_clients.py --probe
    python3 tools/playtest_two_clients.py --run --minutes 5

Exit codes
    0  session survived the full window with no OOS/error pattern found
    1  OOS / desync / error pattern found (evidence dumped)
    2  environment failure (godot/Xvfb/xdotool missing, ports busy, ...)
    3  session did not come up (host never bound or client never connected)

WHY GUI-CLICK AUTOMATION
    The game exposes NO command-line / env / autoload trigger for host or
    join. Verified by reading the tree:

      * scenes/menu/lobby.gd:18-25 is a STUB -- its _on_host_pressed() /
        _on_join_pressed() only print() and change scene; they never touch
        rtso::NetHost.
      * The real handlers live in the in-game HUD:
        src/presentation/main_node.cpp:3634 _on_lobby_host_pressed()
        src/presentation/main_node.cpp:3765 _on_lobby_join_pressed()
        src/presentation/main_node.cpp:3655 _on_lobby_start_pressed()
        and are wired to Button "pressed" signals at main_node.cpp:547/551/560.
      * No OS.get_cmdline_args / user args parsing exists anywhere in
        src/ outside src/0ad (reference-only).

    So this harness drives the actual UI with xdotool on a private Xvfb
    display. It performs a REAL session: real UDP sockets, real TurnManager
    lockstep, real sim hashing.

MEASURED UI COORDINATES (window 3840x1080, canvas stretch 1:1)
    Lobby panel toggle (top bar) : (3024, 46)
    Host button                  : (~41,  419)
    Join button                  : (~145, 419)
    Start button (host only)     : (~353, 419)   # uniform button pitch
    The top HUD HBox overflows, so the Lobby button is only on-screen at
    wide resolutions; the harness forces 3840x1080 for that reason. Each
    click is swept across a small tolerance band so minor layout drift
    does not break the run.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
ARTIFACT_DIR = Path(os.environ.get("HOME", "/tmp")) / "tmp" / "playtest_two_clients"

GODOT = os.environ.get("RTS0_GODOT", str(Path.home() / ".local" / "bin" / "godot"))
SESSION_SCENE = "res://scenes/main.tscn"
WINDOW_W, WINDOW_H = 3840, 1080

LOBBY_HTTP_PORT = 8088          # tools/lobby_server.py default; main_node.h:107
GAME_PORT = 20595               # rtso::PS_DEFAULT_PORT, net_types.h:14

HOST_DISPLAY = ":90"
JOIN_DISPLAY = ":91"

# Verified pixel targets on the 3840x1080 virtual display.
LOBBY_TOGGLE = (3024, 46)
BTN_HOST = (41, 419)
BTN_JOIN = (145, 419)
BTN_START = (276, 419)  # measured 2026-10-06 (broadcast line confirmed); was pitch-estimated 353
# Buttons are ~104px apart; sweep a few px so layout drift still lands.
SWEEP_DX = (-2, 0, 2, 4)
SWEEP_DY = (-3, 0, 3)

# Patterns that mean the session went wrong. Split by severity.
OOS_PATTERNS = [
    r"out[- ]of[- ]sync",
    r"\bOOS\b",
    r"hash mismatch",
    r"hashmismatch",
    r"desync",
    r"sim_state_hash mismatch",
    r"peer_hash=",
]
ERROR_PATTERNS = [
    r"Failed to bind to port",
    r"Failed to create UDP socket",
    r"payload must be exactly",
    r"out of range",
    r"SCRIPT ERROR",
    r"Parse Error",
    r"Cannot open file",
    r"Failed to load resource",
    r"Connection refused",
]
DISCONNECT_PATTERNS = [
    r"disconnected",
    r"timed out",
    r"NET_FLAG_DISCONNECT",
]
# Proof the session actually formed. If none of these appear the run is
# meaningless -- we never got a real two-client session.
SESSION_MARKERS = {
    "host_listening": r"\[NetHost\] Host listening on port",
    "peer_accepted": r"\[NetHost\] Accepted connection from",
    "client_connecting": r"\[NetHost\] Client connecting to",
    "client_connected": r"\[NetHost\] Connected to host, assigned local peer_id",
    "game_start": r"GameStart",
    # Strict OOS-gate markers (2026-10-06): with net_game_started_ gating
    # lockstep turns, a run where Start never fired proves NOTHING (no turns
    # tick -> no OOS possible). These two lines are printed by the engine
    # itself (main_node.cpp) only when the gate opens on each peer.
    "start_host": r"GameStart\] HOST broadcast setup",
    "start_joiner": r"GameStart\] JOINER consumed",
}

EXIT_OK = 0
EXIT_FINDING = 1
EXIT_ENV = 2
EXIT_NO_SESSION = 3


def log(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def warn(msg: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] WARN: {msg}", flush=True)


@dataclass
class Proc:
    name: str
    popen: subprocess.Popen
    log_path: Path
    display: str = ""

    def alive(self) -> bool:
        return self.popen.poll() is None


@dataclass
class Session:
    procs: list[Proc] = field(default_factory=list)
    xvfbs: list[Proc] = field(default_factory=list)
    lobby: Proc | None = None

    def all_procs(self) -> list[Proc]:
        return self.xvfbs + ([self.lobby] if self.lobby else []) + self.procs

    def cleanup(self) -> None:
        """Terminate in reverse order: clients, lobby, then X servers.

        Each process gets its own session (start_new_session=True) so we can
        signal the whole process group and reap children Godot/Xvfb spawned.
        """
        for p in reversed(self.all_procs()):
            _kill(p)


def _kill(p: Proc) -> None:
    if p.popen.poll() is not None:
        return
    try:
        os.killpg(os.getpgid(p.popen.pid), signal.SIGTERM)
    except (ProcessLookupError, PermissionError, OSError):
        try:
            p.popen.terminate()
        except Exception:
            pass
    try:
        p.popen.wait(timeout=8)
        return
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(os.getpgid(p.popen.pid), signal.SIGKILL)
    except (ProcessLookupError, PermissionError, OSError):
        try:
            p.popen.kill()
        except Exception:
            pass
    try:
        p.popen.wait(timeout=5)
    except subprocess.TimeoutExpired:
        warn(f"{p.name}: did not die")


def port_free_udp(port: int) -> bool:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.bind(("127.0.0.1", port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def port_free_tcp(port: int) -> bool:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.bind(("127.0.0.1", port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def require_bins() -> list[str]:
    missing = []
    for b in (GODOT, "Xvfb", "xdotool"):
        path = b if os.path.isabs(b) else shutil.which(b)
        if not path or not os.path.exists(path):
            missing.append(b)
    return missing


def godot_version() -> str:
    try:
        out = subprocess.run([GODOT, "--version"], capture_output=True,
                             text=True, timeout=30).stdout.strip()
        return out.splitlines()[0] if out else "unknown"
    except Exception as exc:  # pragma: no cover - diagnostics only
        return f"unknown ({exc})"


def check_godot_headless(timeout: int = 60) -> tuple[bool, str]:
    """Confirm the project boots headless and the session scene ticks.

    This is the weakest but cheapest sanity gate: it proves the engine and
    the GDExtension load. Headless has no display server, so UI-driven host
    and join cannot be done this way -- that is why --run uses Xvfb.
    """
    try:
        r = subprocess.run(
            [GODOT, "--headless", "--path", str(REPO_ROOT), SESSION_SCENE,
             "--quit-after", "120"],
            capture_output=True, text=True, timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        return False, f"godot headless timed out after {timeout}s ({exc})"
    except Exception as exc:  # pragma: no cover
        return False, f"godot headless failed to launch: {exc}"
    blob = r.stdout + r.stderr
    # The sim prints a [mem] heartbeat line once per second while _process
    # runs; its presence proves the session loop is actually ticking.
    ticking = "[mem]" in blob or "[jobs]" in blob
    if not ticking:
        return False, "godot headless booted but produced no sim heartbeat "\
                      f"(rc={r.returncode})\n{blob[-800:]}"
    return True, f"headless boot OK (rc={r.returncode}, heartbeat present)"


def check_lobby_starts() -> tuple[bool, str, Proc | None]:
    log_path = ARTIFACT_DIR / "probe_lobby.log"
    try:
        p = subprocess.Popen(
            [sys.executable, str(REPO_ROOT / "tools" / "lobby_server.py"),
             "--port", str(LOBBY_HTTP_PORT)],
            stdout=log_path.open("w"), stderr=subprocess.STDOUT,
            start_new_session=True, cwd=str(REPO_ROOT),
        )
    except Exception as exc:
        return False, f"could not launch lobby_server: {exc}", None
    for _ in range(30):
        time.sleep(0.2)
        try:
            with urllib.request.urlopen(
                    f"http://127.0.0.1:{LOBBY_HTTP_PORT}/rooms", timeout=2) as r:
                if r.status == 200:
                    json.loads(r.read().decode())
                    return True, f"lobby_server healthy on :{LOBBY_HTTP_PORT}", p
        except Exception:
            continue
    _kill(Proc("lobby(probe)", p, log_path))
    return False, f"lobby_server did not answer /rooms on :{LOBBY_HTTP_PORT}", None


def do_probe() -> int:
    ARTIFACT_DIR.mkdir(parents=True, exist_ok=True)
    log("=== RTS0 two-client playtest :: PROBE ===")
    log(f"repo        : {REPO_ROOT}")
    log(f"artifacts   : {ARTIFACT_DIR}")

    failures: list[str] = []
    notes: list[str] = []

    log(f"godot       : {GODOT} ({godot_version()})")
    if not os.path.exists(GODOT):
        failures.append(f"godot binary not found at {GODOT}")
    else:
        log(f"godot version: {godot_version()}")

    log("checking helper binaries (Xvfb, xdotool) ...")
    missing = require_bins()
    if missing:
        failures.append("missing required binary/binaries: " + ", ".join(missing))
        for m in missing:
            log(f"  MISSING {m}")
    else:
        log("  Xvfb   : " + (shutil.which("Xvfb") or "found"))
        log("  xdotool: " + (shutil.which("xdotool") or "found"))

    log(f"checking port {GAME_PORT}/udp (lockstep) ...")
    if port_free_udp(GAME_PORT):
        log(f"  udp/{GAME_PORT} free")
    else:
        failures.append(f"udp/{GAME_PORT} already in use -- another session running?")
        log(f"  udp/{GAME_PORT} BUSY")

    log(f"checking port {LOBBY_HTTP_PORT}/tcp (lobby http) ...")
    if port_free_tcp(LOBBY_HTTP_PORT):
        log(f"  tcp/{LOBBY_HTTP_PORT} free")
    else:
        notes.append(f"tcp/{LOBBY_HTTP_PORT} already bound; --run will reuse or "
                     "fail to start its own lobby_server")
        log(f"  tcp/{LOBBY_HTTP_PORT} BUSY (informational)")

    log("booting project headless (proves engine + GDExtension load) ...")
    ok, msg = check_godot_headless()
    log(f"  {'OK  ' if ok else 'FAIL'} {msg}")
    if not ok:
        failures.append(f"godot headless boot failed: {msg}")

    lobby_proc = None
    if not missing:
        log("starting tools/lobby_server.py ...")
        lok, lmsg, lobby_proc = check_lobby_starts()
        log(f"  {'OK  ' if lok else 'FAIL'} {lmsg}")
        if not lok:
            failures.append(lmsg)

    try:
        log("trigger mechanism audit:")
        log("  host/join handlers : src/presentation/main_node.cpp:3634 (host),")
        log("                       src/presentation/main_node.cpp:3765 (join),")
        log("                       src/presentation/main_node.cpp:3655 (start)")
        log("  bound to GUI       : main_node.cpp:547/551/560 (Button 'pressed')")
        log("  scenes/menu/lobby.gd:18-25 is a PRINT-ONLY STUB (never calls NetHost)")
        log("  no cmdline trigger : no OS.get_cmdline_args in src/ outside src/0ad")
        log("  => automation must drive the real UI via xdotool on Xvfb")
    finally:
        if lobby_proc is not None:
            _kill(Proc("lobby(probe)", lobby_proc, ARTIFACT_DIR / "probe_lobby.log"))

    log("--- probe summary ---")
    for n in notes:
        log(f"NOTE: {n}")
    if failures:
        for f in failures:
            log(f"FAIL: {f}")
        log(f"RESULT: environment NOT ready ({len(failures)} failure(s))")
        return EXIT_ENV
    log("RESULT: environment ready -- 'tools/playtest_two_clients.py --run' can "
        "drive a real two-client host/join session")
    return EXIT_OK


def _click_until(display: str, base: tuple[int, int], proc: Proc,
                 pattern: str, timeout: float) -> bool:
    """Click a button at most once per attempt, escalating only on failure.

    Deliberately NOT a blind sweep: repeatedly clicking Host/Join is not
    idempotent. Join in particular re-sends a connect request on every press,
    so a 12-click sweep makes the host churn through peer ids and tear the
    session down. Instead we click the measured centre once, and only if the
    expected log line has not appeared do we try a small offset. Each attempt
    is bounded so a truly dead UI fails fast instead of hammering the button.
    """
    env = {**os.environ, "DISPLAY": display}
    rx = re.compile(pattern)
    bx, by = base
    per_attempt = max(4.0, timeout / 6.0)
    offsets = [(0, 0)] + [(dx, dy) for dy in SWEEP_DY for dx in SWEEP_DX if dx or dy]
    for i, (dx, dy) in enumerate(offsets):
        subprocess.run(["xdotool", "mousemove", str(bx + dx), str(by + dy)],
                       env=env, capture_output=True)
        time.sleep(0.2)
        subprocess.run(["xdotool", "click", "1"], env=env, capture_output=True)
        deadline = time.time() + per_attempt
        while time.time() < deadline:
            if rx.search(_read(proc.log_path)):
                log(f"  {proc.name}: button hit at offset ({dx},{dy})")
                return True
            if not proc.alive():
                break
            time.sleep(0.5)
        if rx.search(_read(proc.log_path)):
            return True
        if not proc.alive():
            warn(f"{proc.name}: process died while clicking {base}")
            return False
    return bool(rx.search(_read(proc.log_path)))


def _click_once(display: str, x: int, y: int) -> None:
    env = {**os.environ, "DISPLAY": display}
    subprocess.run(["xdotool", "mousemove", str(x), str(y)], env=env,
                   capture_output=True)
    subprocess.run(["xdotool", "click", "1"], env=env, capture_output=True)


def _focus_window(display: str, timeout: float = 30.0) -> bool:
    """Activate the RTS0 window on `display` so synthetic clicks land."""
    env = {**os.environ, "DISPLAY": display}
    deadline = time.time() + timeout
    while time.time() < deadline:
        r = subprocess.run(["xdotool", "search", "--name", "RTS0"], env=env,
                           capture_output=True, text=True)
        ids = [ln.strip() for ln in r.stdout.splitlines() if ln.strip()]
        if ids:
            for wid in ids:
                subprocess.run(["xdotool", "windowactivate", wid], env=env,
                               capture_output=True)
                subprocess.run(["xdotool", "windowfocus", wid], env=env,
                               capture_output=True)
            return True
        time.sleep(1.0)
    return False


def _launch_xvfb(display: str, log_path: Path) -> Proc:
    num = display.lstrip(":")
    p = subprocess.Popen(
        ["Xvfb", display, "-screen", "0", f"{WINDOW_W}x{WINDOW_H}x24"],
        stdout=log_path.open("w"), stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    time.sleep(2.5)
    if p.poll() is not None:
        raise RuntimeError(f"Xvfb {display} exited immediately (rc={p.returncode}); "
                           f"see {log_path}")
    return Proc(f"Xvfb{display}", p, log_path, display)


def _launch_client(name: str, display: str, log_path: Path, session: Session) -> Proc:
    env = {**os.environ, "DISPLAY": display}
    env.setdefault("XDG_RUNTIME_DIR", str(ARTIFACT_DIR / "xdg"))
    Path(env["XDG_RUNTIME_DIR"]).mkdir(parents=True, exist_ok=True)
    # stdbuf forces line buffering so std::cout from the native NetHost /
    # TurnManager is actually flushed to the log while we tail it. Without
    # this the C++ side is block-buffered and we lose every NetHost line.
    cmd = ["stdbuf", "-oL", "-eL", GODOT, "--path", str(REPO_ROOT),
           SESSION_SCENE, "--rendering-driver", "opengl3",
           "--resolution", f"{WINDOW_W}x{WINDOW_H}"]
    p = subprocess.Popen(cmd, stdout=log_path.open("w"),
                         stderr=subprocess.STDOUT, env=env,
                         start_new_session=True, cwd=str(REPO_ROOT))
    proc = Proc(name, p, log_path, display)
    session.procs.append(proc)
    return proc


def _read(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except OSError:
        return ""


def _scan(text: str, patterns: list[str]) -> list[tuple[str, str]]:
    hits = []
    for pat in patterns:
        for m in re.finditer(pat, text, re.IGNORECASE):
            line = text.count("\n", 0, m.start()) + 1
            src_line = text.splitlines()[line - 1] if line - 1 < len(text.splitlines()) else ""
            hits.append((pat, src_line.strip()))
            break  # one sample per pattern is enough evidence
    return hits


def _wait_for(proc: Proc, pattern: str, timeout: float) -> bool:
    rx = re.compile(pattern)
    deadline = time.time() + timeout
    while time.time() < deadline:
        if rx.search(_read(proc.log_path)):
            return True
        if not proc.alive():
            return bool(rx.search(_read(proc.log_path)))
        time.sleep(0.5)
    return False


# The session scene streams a [mem] line from RtsoMain::_process once per
# second. Its appearance proves the scene finished loading AND the main loop
# is actually running, i.e. the UI can accept input. Clicking before this
# lands on a still-constructing control tree and silently does nothing.
READY_PATTERN = r"\[mem\]"


def _wait_ready(proc: Proc, timeout: float) -> bool:
    """Block until the scene's main loop emits its first heartbeat.

    Two clients at once saturate the CPU (7 sim worker threads each), so
    first-frame latency measured ~60-75s here versus ~30s for a single
    client. Callers must budget generously for this.
    """
    deadline = time.time() + timeout
    told = False
    while time.time() < deadline:
        if re.search(READY_PATTERN, _read(proc.log_path)):
            return True
        if not proc.alive():
            warn(f"{proc.name}: process exited (rc={proc.popen.returncode}) "
                 f"before the scene became interactive")
            return bool(re.search(READY_PATTERN, _read(proc.log_path)))
        if not told and timeout > 45:
            log(f"    ({proc.name}: still loading scene, two clients compete for "
                f"CPU; first heartbeat can take 60-90s ...)")
            told = True
        time.sleep(2.0)
    return False


def _bootstrap_window(display: str, name: str, proc: Proc, startup: float) -> bool:
    """Wait until the client is genuinely interactive, then open the lobby panel.

    Three gates, in order: the process is alive, the window exists, and the
    scene's main loop has produced a heartbeat. Only then is a click meaningful.
    """
    if not _wait_ready(proc, timeout=startup):
        warn(f"{name}: scene never reached an interactive state within {startup:.0f}s")
        return False
    if not _focus_window(display, timeout=30.0):
        warn(f"{name}: window appeared but could not be focused")
        return False
    log(f"{name}: scene ready (sim heartbeat seen), opening lobby panel")
    time.sleep(1.0)
    _click_once(display, *LOBBY_TOGGLE)
    time.sleep(2.5)
    return True


def do_run(minutes: float, hard_timeout: float, skip_start: bool) -> int:
    ARTIFACT_DIR.mkdir(parents=True, exist_ok=True)
    session = Session()
    host_log = ARTIFACT_DIR / "host.log"
    join_log = ARTIFACT_DIR / "join.log"
    lobby_log = ARTIFACT_DIR / "lobby_server.log"

    log("=== RTS0 two-client playtest :: RUN ===")
    log(f"window {WINDOW_W}x{WINDOW_H}  host display {HOST_DISPLAY}  "
        f"join display {JOIN_DISPLAY}")
    log(f"hold time: {minutes:.2f} min   hard cap: {hard_timeout:.0f}s")

    hard_deadline = time.time() + hard_timeout
    rc = EXIT_ENV
    try:
        missing = require_bins()
        if missing:
            log(f"FAIL: missing binaries: {', '.join(missing)}")
            return EXIT_ENV
        if not os.path.exists(GODOT):
            log(f"FAIL: godot missing at {GODOT}")
            return EXIT_ENV

        log(f"checking ports (udp/{GAME_PORT}, tcp/{LOBBY_HTTP_PORT}) ...")
        if not port_free_udp(GAME_PORT):
            log(f"FAIL: udp/{GAME_PORT} busy -- stale session? refusing to run")
            return EXIT_ENV

        log("starting Xvfb displays ...")
        session.xvfbs.append(_launch_xvfb(HOST_DISPLAY, ARTIFACT_DIR / "xvfb_host.log"))
        session.xvfbs.append(_launch_xvfb(JOIN_DISPLAY, ARTIFACT_DIR / "xvfb_join.log"))

        log("starting tools/lobby_server.py ...")
        lp = subprocess.Popen(
            [sys.executable, str(REPO_ROOT / "tools" / "lobby_server.py"),
             "--port", str(LOBBY_HTTP_PORT)],
            stdout=lobby_log.open("w"), stderr=subprocess.STDOUT,
            start_new_session=True, cwd=str(REPO_ROOT),
        )
        session.lobby = Proc("lobby_server", lp, lobby_log)
        time.sleep(1.5)
        if lp.poll() is not None:
            log(f"FAIL: lobby_server exited rc={lp.returncode}; see {lobby_log}")
            return EXIT_ENV
        log(f"  lobby_server up on 127.0.0.1:{LOBBY_HTTP_PORT}")

        log("launching HOST client ...")
        host = _launch_client("host", HOST_DISPLAY, host_log, session)
        log("launching JOIN client ...")
        join = _launch_client("join", JOIN_DISPLAY, join_log, session)

        log("waiting for both clients to boot and render a window ...")
        if not _bootstrap_window(HOST_DISPLAY, "host", host, min(240.0, max(5.0, hard_deadline - time.time()))):
            log("FAIL: host window never appeared")
            return EXIT_NO_SESSION
        if not _bootstrap_window(JOIN_DISPLAY, "join", join, min(240.0, max(5.0, hard_deadline - time.time()))):
            log("FAIL: join window never appeared")
            return EXIT_NO_SESSION

        log("HOST: clicking Host button")
        if not _click_until(HOST_DISPLAY, BTN_HOST, host,
                            SESSION_MARKERS["host_listening"], timeout=30):
            log("FAIL: host never reported 'Host listening'")
            log("  host log tail:")
            for ln in _read(host_log).splitlines()[-25:]:
                log(f"    | {ln}")
            return EXIT_NO_SESSION
        log("  ok: " + next((ln for ln in _read(host_log).splitlines()
                            if "Host listening" in ln), "host listening"))

        log("JOIN: clicking Join button")
        if not _click_until(JOIN_DISPLAY, BTN_JOIN, join,
                            SESSION_MARKERS["client_connecting"], timeout=30):
            log("FAIL: join client never reported 'Client connecting'")
            log("  join log tail:")
            for ln in _read(join_log).splitlines()[-25:]:
                log(f"    | {ln}")
            return EXIT_NO_SESSION
        log("  ok: " + next((ln for ln in _read(join_log).splitlines()
                            if "Client connecting" in ln), "client connecting"))

        log("waiting for peer handshake (host accepts the client) ...")
        if not _wait_for(host, SESSION_MARKERS["peer_accepted"], timeout=45):
            log("WARN: host never logged 'Accepted connection' -- session may be "
                "one-sided; continuing to observe anyway")
        else:
            log("  ok: " + next((ln for ln in _read(host_log).splitlines()
                                if "Accepted connection" in ln), "peer accepted"))

        if not skip_start:
            log("HOST: clicking Start (host-only; broadcasts NMT_GAME_START with "
                "map+seed so both peers build the identical world)")
            # BTN_START (353) was derived from uniform button pitch, NOT measured
            # (only Host=41 and Join=145 were verified empirically). Click the
            # primary coordinate, then sweep nearby candidates until the host's
            # own "HOST broadcast setup" line proves the handler actually ran —
            # otherwise the run would pass vacuously (gate closed = no turns).
            hit = None
            for sx in (BTN_START[0], 302, 276, 380, 405, 250, 430, 221, 460):
                _click_once(HOST_DISPLAY, sx, BTN_START[1])
                deadline = time.time() + 2.5
                while time.time() < deadline:
                    if "HOST broadcast setup" in _read(host_log):
                        hit = sx
                        break
                    time.sleep(0.25)
                if hit is not None:
                    break
            if hit is None:
                log("WARN: Start produced no broadcast line — strict start "
                    "markers will fail this run (button layout drift?)")
            else:
                log(f"  ok: Start hit at x={hit} (host broadcast line seen)")
                time.sleep(2.0)  # let the joiner consume NMT_GAME_START
        else:
            log("skipping Start (--skip-start): the joiner will NOT receive the "
                "seed sync, so an OOS is expected and will be reported as such")

        # --- hold the session -------------------------------------------------
        # Snapshot log lengths now so teardown-induced disconnects can be
        # excluded from the findings below.
        hold_start_idx = _size(host_log)
        join_start_idx = _size(join_log)
        hold_s = minutes * 60.0
        log(f"holding session for {hold_s:.0f}s (poll every 15s) ...")
        t0 = time.time()
        killed_early = False
        while time.time() - t0 < hold_s:
            if time.time() > hard_deadline:
                log("WARN: hard timeout reached, ending hold early")
                killed_early = True
                break
            for p in (host, join):
                if not p.alive():
                    log(f"WARN: {p.name} exited unexpectedly rc={p.popen.returncode}")
                    break
            el = int(time.time() - t0)
            if el % 60 < 15:
                log(f"  t+{el//60}m{el%60:02d}s  host={_size(host_log)}B "
                    f"join={_size(join_log)}B")
            time.sleep(15)

        elapsed = time.time() - t0
        log(f"hold finished after {elapsed/60:.2f} min"
            + (" (hard-capped)" if killed_early else ""))

        # --- flush line buffers so we capture everything ----------------------
        for p in (host, join):
            try:
                p.popen.send_signal(signal.SIGINT)
            except Exception:
                pass
        time.sleep(3.0)

        host_text = _read(host_log)
        join_text = _read(join_log)
        combined = host_text + "\n" + join_text

        # --- did a real session actually form? --------------------------------
        markers = {k: bool(re.search(p, combined)) for k, p in SESSION_MARKERS.items()}
        # The host seeing sync checks from a live peer is the strongest proof
        # the joiner finished its handshake and both sims are stepping.
        markers["sync_exchange"] = bool(re.search(r"\[TurnManager\].*peer=", combined))
        log("--- session markers ---")
        for k, v in markers.items():
            log(f"  {'YES' if v else 'no ':<4} {k}")

        oos = _scan(combined, OOS_PATTERNS)
        errs = _scan(combined, ERROR_PATTERNS)
        # Disconnects only count if they happened DURING the hold window.
        # We kill both clients at teardown, which always emits a
        # "Peer N disconnected" line on the host -- that is self-inflicted
        # and must not be reported as a playtest finding.
        held_host = host_text[hold_start_idx:]
        held_join = join_text[join_start_idx:]
        disc = _scan(held_host + "\n" + held_join, DISCONNECT_PATTERNS)

        log("--- findings ---")
        log(f"  OOS/desync patterns : {len(oos)}")
        log(f"  error patterns     : {len(errs)}")
        log(f"  disconnect patterns : {len(disc)}")

        findings = oos + errs + disc
        if findings:
            log("EVIDENCE (first hit per pattern):")
            for pat, line in findings:
                log(f"  [{pat}] {line}")

        host_dead = not host.alive()
        join_dead = not join.alive()
        if host_dead or join_dead:
            log(f"clients dead at teardown: host={'yes' if host_dead else 'no'} "
                f"join={'yes' if join_dead else 'no'}")

        established = (markers["peer_accepted"]
                       and (markers["client_connected"] or markers["sync_exchange"]))
        if established and not skip_start and not (
                markers["start_host"] and markers["start_joiner"]):
            log("VERDICT: peers connected but NMT_GAME_START never completed on "
                "both sides (start_host="
                f"{markers['start_host']}, start_joiner={markers['start_joiner']})"
                " — the lockstep gate stayed closed, no turns ticked, and the "
                "run proved nothing.")
            rc = EXIT_NO_SESSION
            established = False
        if not established:
            log("VERDICT: a genuine two-client session was never established "
                "(no completed handshake). Nothing was actually proven.")
            rc = EXIT_NO_SESSION
        elif findings:
            log("VERDICT: session formed but OOS/error evidence was found.")
            rc = EXIT_FINDING
        else:
            log(f"VERDICT: two-client session survived {elapsed/60:.2f} min with "
                "no OOS, desync, disconnect or error pattern.")
            rc = EXIT_OK
        return rc

    except Exception as exc:  # noqa: BLE001 - report, never mask
        log(f"ERROR during session: {type(exc).__name__}: {exc}")
        rc = EXIT_ENV
        return rc
    finally:
        log("tearing down ...")
        session.cleanup()
        # Only report this run's artifacts, not unrelated files that may
        # already sit in the shared directory.
        mine = {p.log_path.name for p in session.all_procs()}
        log(f"artifacts in {ARTIFACT_DIR}:")
        for f in sorted(ARTIFACT_DIR.iterdir()) if ARTIFACT_DIR.exists() else []:
            if f.is_file() and f.name in mine:
                log(f"  {f.name} ({_size(f)}B)")
        log(f"EXIT {rc}")


def _size(p: Path) -> int:
    try:
        return p.stat().st_size
    except OSError:
        return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="RTS0 two-client host/join playtest automation "
                    "(docs/PLAN.md P1 REAL gate).")
    ap.add_argument("--probe", action="store_true",
                    help="validate environment: godot headless boot, lobby "
                         "server, ports, Xvfb/xdotool. Exit 2 on env failure.")
    ap.add_argument("--run", action="store_true",
                    help="run a real two-client host/join session")
    ap.add_argument("--minutes", type=float, default=5.0,
                    help="how long to hold the session (default: 5)")
    ap.add_argument("--timeout", type=float, default=0.0,
                    help="hard cap in seconds for --run (default: "
                         "minutes*60 + 300 setup/teardown allowance; 0 = auto)")
    args = ap.parse_args(argv)

    if not args.probe and not args.run:
        ap.print_help()
        return EXIT_ENV

    if args.probe:
        return do_probe()

    hard = args.timeout if args.timeout > 0 else args.minutes * 60 + 300
    return do_run(args.minutes, hard, skip_start=False)


if __name__ == "__main__":
    sys.exit(main())
