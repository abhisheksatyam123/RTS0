# Task File

## Tasks
- [x] T1 Start `qcode-server` in /home/abhi/project/RTS0 bound to all interfaces (network reachable)

## Systems
- `qcode-server` binary: `/home/abhi/.local/bin/qcode-server`; usage: `qcode-server [--port PORT] [--log-dir DIR]`
- Binds `0.0.0.0` by default (no host flag) -> "QCode server listening on http://0.0.0.0:<port>"
- `qcode-cli --server <url>` is the non-TTY client; default server URL `http://localhost:9080`
- Server DB: `/home/abhi/.qcode.db`; providers from `/home/abhi/.config/opencode/opencode.json`
- Web UI served from `/home/abhi/project/qcode/apps/webui/src`
- Host IPs: LAN `192.168.1.23` (wlo1), Tailscale `100.105.86.72` (tailscale0)

## Log
- 2026-10-21 18:47 [lead] Started qcode-server pid 6156 on 0.0.0.0:9080, cwd=/home/abhi/project/RTS0, logs in logs/qcode/; verified HTTP 200 via localhost and 192.168.1.23
