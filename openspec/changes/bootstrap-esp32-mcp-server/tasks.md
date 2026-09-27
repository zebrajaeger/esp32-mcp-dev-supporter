# Tasks

## 1. PlatformIO Firmware Foundation

- [ ] 1.1 Create the PlatformIO project targeting the ESP32 DevKit V1 with ESP-IDF and C++ entry points; verify `pio run` completes for the target environment.
- [ ] 1.2 Add startup diagnostics that identify the firmware version and initial network state; verify the messages appear on the serial monitor after upload.
- [ ] 1.3 Add a committed WiFi configuration template, an ignored local credential file, and a build-time missing-configuration diagnostic; verify a build succeeds with local credentials and fails clearly without them.
- [ ] 1.4 Add setup documentation covering build/upload, local credential setup, trusted-LAN limitation, and the OpenCode remote MCP URL; verify the documented commands and file paths match the project tree.

## 2. Device Connectivity

- [ ] 2.1 Implement ESP-IDF WiFi station initialization, connection event handling, and reconnect logging using the local credentials; verify the device obtains an address on the configured network and reconnect attempts are logged after a disconnect.
- [ ] 2.2 Start mDNS after the device receives an IP address with hostname `esp32-mcp`; verify a same-network machine resolves `esp32-mcp.local` to the device address.
- [ ] 2.3 Start the ESP-IDF HTTP server on port 80 once network connectivity is available and add a minimal health response if used for diagnostics; verify an HTTP client can establish a connection to the device.

## 3. MCP Protocol and Status Tool

- [ ] 3.1 Implement `POST /mcp` JSON parsing, JSON-RPC validation, and response/error encoding; verify malformed JSON-RPC and unsupported methods return errors without a firmware restart.
- [ ] 3.2 Implement MCP `initialize` and accept `notifications/initialized` for a Streamable HTTP client; verify OpenCode can initialize against `http://esp32-mcp.local/mcp`.
- [ ] 3.3 Implement `tools/list` with schemas for `system.status`, `gpio.read`, and `gpio.write`; verify the initialized OpenCode MCP connection exposes exactly those tools.
- [ ] 3.4 Implement `system.status` with firmware version, live WiFi state, assigned IP when connected, mDNS hostname, uptime, and available heap; verify every field is returned through an MCP tool call.

## 4. Restricted GPIO Tools

- [ ] 4.1 Initialize GPIO 2 as the DevKit V1 onboard LED output and isolate it behind a GPIO service with a one-pin allowlist; verify the booted firmware configures no other GPIO through the MCP path.
- [ ] 4.2 Implement `gpio.write` to require `pin: 2` and a Boolean `level`, set GPIO 2, and return the resulting state; verify OpenCode turns the onboard LED on and off.
- [ ] 4.3 Implement `gpio.read` to require `pin: 2` and return the current Boolean GPIO 2 level; verify it reports the state set by `gpio.write`.
- [ ] 4.4 Return MCP tool errors for missing/invalid arguments and all GPIO values other than 2 without invoking GPIO access; verify requests for an unsupported pin leave the onboard LED state unchanged.

## 5. End-to-End Verification

- [ ] 5.1 Build and upload the firmware with developer-local WiFi credentials; verify serial diagnostics show WiFi connection, mDNS registration, and HTTP server startup.
- [ ] 5.2 Add the device as an unauthenticated remote MCP server in the local OpenCode environment at `http://esp32-mcp.local/mcp`; verify OpenCode discovers and calls `system.status`.
- [ ] 5.3 Execute an OpenCode-driven LED on, read, and off sequence using `gpio.write` and `gpio.read`; verify the physical onboard LED and returned levels agree at every step.
