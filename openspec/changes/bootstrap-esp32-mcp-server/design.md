# Design

## Context

The repository contains only OpenSpec scaffolding and has no firmware implementation. This change establishes the initial vertical path described in `proposal.md`: a real ESP32 DevKit V1 becomes a remote MCP server that OpenCode can invoke over the local network.

The target has limited RAM and runs in a trusted development LAN. The scope therefore favors a bounded MCP surface, native ESP-IDF services, and a synchronous request/response flow rather than full general-purpose server features.

## Goals / Non-Goals

**Goals:**
- Produce a repeatable PlatformIO/ESP-IDF C++ firmware baseline for the ESP32 DevKit V1.
- Establish WiFi station connectivity, mDNS discovery, and port-80 HTTP serving before exposing GPIO operations.
- Implement enough MCP Streamable HTTP and JSON-RPC behavior for OpenCode to initialize, discover, and call the initial tools.
- Limit physical effects to the onboard LED on GPIO 2.

**Non-Goals:**
- WiFi provisioning portals, BLE provisioning, stored credentials, or an access-point fallback.
- TLS, authentication, authorization, or use outside a trusted development LAN.
- Server-sent events, streaming tool responses, MCP resources, prompts, sampling, or subscriptions.
- GPIO support beyond GPIO 2, including run-time mode configuration.
- A checked-in OpenCode configuration; the device URL belongs to each developer environment.

## Decisions

### Use PlatformIO with ESP-IDF and C++

PlatformIO supplies the build/upload workflow while ESP-IDF provides the native WiFi lifecycle, HTTP server, mDNS, GPIO, and system-information APIs. C++ organizes the MCP request dispatch and tool boundary, while firmware services use the ESP-IDF C APIs directly.

Arduino was considered for faster initial sketches. It is not selected because the target is a network service whose WiFi and HTTP lifecycle should remain directly controllable as the MCP surface grows.

### Keep WiFi credentials local and fail builds without them

The project will include a committed configuration template and a git-ignored local configuration file containing SSID and password. The build will require the local configuration rather than embedding fallback credentials or silently producing a non-networked firmware image.

Runtime provisioning was considered but deferred because it introduces a separate HTTP UI, persistence strategy, reset flow, and security boundary before the core OpenCode-to-GPIO path is proven.

### Start network services in dependency order

Firmware startup will initialize serial diagnostics and GPIO 2, then connect to WiFi station mode. Once a network address exists, it will start mDNS with hostname `esp32-mcp` and the port-80 HTTP server. Network disconnect events will be logged and trigger connection recovery; status is always derived from current state rather than assumed successful startup.

```text
boot
  |
  v
serial diagnostics + GPIO 2 output
  |
  v
WiFi station connection
  |
  +--> disconnected: retry and report status
  |
  v
IP assigned
  |
  +--> mDNS: esp32-mcp.local
  |
  +--> HTTP: port 80, POST /mcp
```

### Implement a bounded MCP Streamable HTTP subset

`POST /mcp` will accept one JSON-RPC request and return one JSON-RPC response. The dispatcher will recognize `initialize`, `tools/list`, and `tools/call`; `notifications/initialized` can be accepted without a response when a client sends it. Responses will identify the server and advertise tool capability. No session state or SSE connection is required for the initial synchronous tools.

The service will use the ESP-IDF HTTP server and cJSON supplied by ESP-IDF. A handwritten parser is not selected because protocol errors and input validation are core compatibility concerns. A full desktop MCP SDK is not selected because it adds unneeded runtime and memory cost for a fixed three-tool surface.

### Separate protocol dispatch from hardware access

The MCP layer parses and validates JSON-RPC, maps tool names to handlers, and encodes MCP tool results or errors. A small GPIO service owns GPIO 2 initialization, reads, writes, and its one-pin allowlist. This ensures a malformed request cannot bypass the hardware boundary.

```text
HTTP POST /mcp
       |
       v
JSON-RPC validation and method dispatch
       |
       +--> initialize
       +--> tools/list
       +--> tools/call
                  |
                  +--> system.status
                  +--> gpio.read  ---> GPIO service ---> GPIO 2
                  +--> gpio.write ---> GPIO service ---> GPIO 2
```

`gpio.read` and `gpio.write` require `pin: 2`. Any other pin is rejected before invoking the ESP-IDF GPIO API. GPIO 2 is configured as an output exactly once at startup, so no public `gpio.configure` tool is needed in this change.

### Treat HTTP without security as an explicit environmental constraint

The service will intentionally accept unauthenticated plain HTTP to meet the first-phase objective. This is not a generalized security design: README-level setup guidance and the WiFi template must state that the endpoint can control GPIO 2 and is safe only on a trusted development LAN.

## Risks / Trade-offs

- [The onboard LED mapping varies across some ESP32 board clones] -> Target the user-confirmed DevKit V1 mapping on GPIO 2 and make the mapping a single named firmware constant for a later board-specific change.
- [GPIO 2 is a bootstrapping pin] -> Configure it only after boot, avoid external pull circuitry in the initial hardware setup, and prohibit other GPIO operations.
- [mDNS availability varies by operating system and network] -> Keep the current IP address in `system.status` and serial diagnostics as a fallback connection path.
- [OpenCode and MCP protocol versions may require stricter behavior than the initial subset] -> Keep HTTP protocol handling isolated so response headers, session support, or SSE can be added without changing tools or GPIO access.
- [No authentication or encryption exposes controls to the LAN] -> Explicitly constrain use to an isolated trusted development network; do not represent this release as suitable for production or shared networks.
- [WiFi connection can fail or drop after boot] -> Report live connection state, IP availability, and retries through diagnostics and `system.status` rather than making GPIO behavior depend on a stale connection result.

## Migration Plan

1. Create and build the PlatformIO project using the ESP-IDF target configuration.
2. Copy the WiFi configuration template to its ignored local counterpart and enter trusted-LAN credentials.
3. Upload firmware and use serial diagnostics to confirm WiFi connection, mDNS registration, and HTTP startup.
4. Configure OpenCode with the remote URL `http://esp32-mcp.local/mcp` in the developer environment.
5. Call `system.status`, then verify `gpio.write` and `gpio.read` using the onboard LED.

Rollback consists of removing the remote MCP entry from OpenCode or powering down/reflashing the development board. No persistent protocol data or shared service migration is introduced.

## Open Questions

- The firmware version string format can be selected during implementation, provided it is stable enough to identify a running build in `system.status` and serial diagnostics.
