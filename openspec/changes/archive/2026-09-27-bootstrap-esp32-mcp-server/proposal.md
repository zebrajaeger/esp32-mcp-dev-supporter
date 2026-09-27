# Proposal

## Why

Embedded development needs a direct, agent-accessible interface to an ESP32 instead of manual serial or browser-driven hardware control. Establishing a minimal, observable MCP server now validates the complete path from OpenCode to a real GPIO before broader embedded development tools are added.

## What Changes

- Create a PlatformIO firmware project for an ESP32 DevKit V1 using ESP-IDF and C++.
- Connect the device to a developer-provided WiFi network in station mode using local, untracked build configuration.
- Advertise the device through mDNS as `esp32-mcp.local`.
- Expose an unauthenticated HTTP MCP endpoint at `http://esp32-mcp.local/mcp` on port 80 for OpenCode.
- Implement MCP initialization, tool discovery, and tool calls for device status plus GPIO 2 read/write operations.
- Configure GPIO 2 as the onboard LED output and reject GPIO access to every other pin in this initial release.

## Capabilities

### New Capabilities
- `esp32-mcp-server`: Provides an ESP32-hosted HTTP MCP server that reports device status and controls the DevKit V1 onboard LED through GPIO 2.

### Modified Capabilities

- None.

## Impact

- Adds the initial firmware source tree, PlatformIO configuration, and local WiFi configuration template.
- Requires PlatformIO with an ESP32/ESP-IDF toolchain and an ESP32 DevKit V1 connected to the local development network.
- Adds an OpenCode remote-MCP integration endpoint, but does not create or modify an OpenCode configuration file because the device address is runtime environment-specific.
- Deliberately provides neither authentication nor transport encryption; use is limited to a trusted development LAN.
