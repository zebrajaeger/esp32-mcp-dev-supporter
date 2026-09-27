# Proposal

## Why

Developers can call MCP tools, but cannot inspect the ESP32's current network and GPIO state in a browser. A read-only HTTP status dashboard makes the device's active configuration and pin classifications visible without expanding its hardware-control surface.

## What Changes

- Add a browser-facing status dashboard at `GET /` on the existing port-80 HTTP server.
- Add `GET /api/status` that returns a JSON snapshot of firmware, network, and GPIO inventory state.
- Show the complete ESP32 GPIO inventory with direction, level where safe to sample, readable/writable status, firmware-management status, and classifications for special pins.
- Refresh the browser dashboard from `/api/status` automatically every two seconds.
- Keep all new HTTP endpoints strictly read-only; MCP remains the sole control interface and continues to allow writes only for GPIO 2.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `esp32-mcp-server`: Adds a read-only HTTP status API and dashboard that expose device and GPIO inventory state without changing MCP GPIO permissions.

## Impact

- Extends the ESP-IDF HTTP server and GPIO status modeling.
- Adds small embedded HTML, CSS, and JavaScript payloads to firmware and corresponding setup documentation.
- Uses the existing trusted-LAN, unauthenticated HTTP boundary; the dashboard exposes observations only and has no HTTP control routes.
