# 01 — Protocol V1 基础帧协议与端到端 Tracer Bullet

**What to build:**
Deliver the end-to-end Protocol V1 foundation and initial cross-process Tracer Bullet. The elevated C++ collector daemon encodes live 4 Hz network traffic ticks over an authenticated local Named Pipe using a fixed 12-byte wire header. A minimal .NET 10 WPF UI connects via `NamedPipeClient`, performs the `Hello`/`HelloAck` handshake, parses ticks on a background I/O thread, replaces state atomically in `TelemetryStore`, and renders live download/upload numbers on screen, proving the entire vertical pipeline works end-to-end.

**Blocked by:** None — can start immediately

**Status:** complete

- [x] C++ `nfm_ipc` implements Protocol V1 12-byte binary framing (`Magic: "NFM1"`, `Version: 1`, `MessageType: uint16`, `PayloadLength: uint32`) with a strict 256 KiB payload ceiling.
- [x] C++ collector server creates an authenticated local Named Pipe with explicit security descriptor restricting access to the launching user SID and `LocalSystem`.
- [x] C++ collector verifies client process identity (`GetNamedPipeClientProcessId`) and validates `Hello` handshake parameters before responding with `HelloAck` containing `collector_instance_id`, `collector_pid`, and capabilities.
- [x] C++ collector pushes `TrafficTick` (Type `0x0003`) at 4 Hz (250 ms) containing `seq`, `monotonic_ms`, `download_bytes_per_sec`, and `upload_bytes_per_sec`.
- [x] Cross-language golden JSON protocol test vectors in `tests/fixtures/` pass verification in both C++ CTest and C# xUnit test suites for framing, headers, limits, and valid/truncated/corrupt payloads.
- [x] .NET 10 WPF project in `apps/nfm-ui/` establishes a non-blocking `NamedPipeClient`, ingests `TrafficTick` packets, and updates a thread-safe atomic `TelemetryStore`.
- [x] Minimal WPF window displays live download and upload rates received from the 4 Hz stream without UI thread locking or IPC thread presentation mutation.
- [x] Automated Hybrid E2E test runs unattended in CI/local builds (synthetic C++ collector host + C# client) and passes with zero manual interaction or UAC prompts.
- [x] Legacy C++ Win32 UI target and existing tests remain intact, buildable, and passing.
