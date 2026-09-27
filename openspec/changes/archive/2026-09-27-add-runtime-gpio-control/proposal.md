# Proposal

## Why

The MCP server currently exposes only GPIO 2 as a fixed LED output, while the status dashboard presents a wide, read-only GPIO table. Embedded development needs MCP-driven control of all safely usable GPIOs, with an optional compact browser control surface that reflects the same runtime state.

## What Changes

- **BREAKING** Replace the GPIO 2-only MCP input contracts with general, validated GPIO access for all supported pins.
- Add an MCP `gpio.configure` tool to explicitly configure an allowed GPIO as an input with a selected pull mode or as an output with a required initial level.
- Extend `gpio.read` and `gpio.write` to operate on allowed GPIOs; require an explicit output configuration before writes.
- Initialize every allowed GPIO at boot as an input with no pull resistor; retain this configuration only in RAM until reset or power loss.
- Keep GPIO 1 and 3 (UART) and GPIO 6 through 11 (flash) completely inaccessible through MCP and the dashboard.
- Restrict GPIO 34 through 39 to input mode with no internal pull resistor.
- Make the HTTP status snapshot report GPIO runtime mode, pull configuration, level, writability, and access restrictions.
- Replace the wide dashboard table with a responsive GPIO card layout and add browser controls that use the same GPIO validation and runtime state as MCP.
- Retain the existing trusted-LAN, unauthenticated plain-HTTP deployment boundary.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `esp32-mcp-server`: Expand the GPIO contract from fixed onboard-LED control and read-only status into guarded runtime GPIO configuration, MCP control, and an optional browser control surface.

## Impact

- Affects `src/gpio_service.cpp`, `include/gpio_service.h`, and `src/mcp_server.cpp`.
- Changes the MCP `gpio.read` and `gpio.write` schemas and adds `gpio.configure`.
- Adds mutating dashboard HTTP routes alongside the existing read-only status routes.
- Updates dashboard UI, README usage and security guidance, and the existing ESP32 MCP server specification.
