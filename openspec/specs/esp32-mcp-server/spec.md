# ESP32 MCP Server Specification

## Purpose

Provide OpenCode with a direct, network-accessible MCP interface for observing an ESP32 and operating its onboard LED during embedded development.

## Requirements

### Requirement: ESP32 development firmware project
The system SHALL provide a PlatformIO firmware project targeting an ESP32 DevKit V1, using ESP-IDF and C++ as its firmware environment. The firmware SHALL emit serial startup diagnostics sufficient to identify the firmware version and initial network state.

#### Scenario: Firmware is built and started on the target board
- **WHEN** the firmware is built with the ESP32 PlatformIO environment and uploaded to an ESP32 DevKit V1
- **THEN** the board SHALL boot the firmware and emit startup diagnostics on its serial console

### Requirement: WiFi station connectivity
The system SHALL load WiFi SSID and password from a local development configuration that is excluded from version control. It SHALL connect to that network in station mode and make its connection state available through device status.

#### Scenario: Valid local WiFi configuration is supplied
- **WHEN** valid WiFi credentials are supplied through the local development configuration and the configured network is available
- **THEN** the device SHALL connect in station mode and obtain a network address

#### Scenario: Local WiFi configuration is absent
- **WHEN** firmware is built without the required local WiFi configuration
- **THEN** the build SHALL fail with a diagnostic identifying the missing configuration

### Requirement: Stable local device discovery
The system SHALL advertise its reachable hostname as `esp32-mcp.local` through multicast DNS after joining WiFi.

#### Scenario: Device joins the configured network
- **WHEN** the device has obtained a network address through WiFi station mode
- **THEN** a client on the same local network SHALL be able to resolve `esp32-mcp.local` to the device

### Requirement: HTTP MCP protocol endpoint
The system SHALL expose an unauthenticated MCP Streamable HTTP endpoint at `POST /mcp` on port 80. The endpoint SHALL process JSON-RPC requests for `initialize`, `tools/list`, and `tools/call`; it SHALL return JSON-RPC errors for unsupported methods, malformed requests, and unknown tools.

#### Scenario: OpenCode initializes the MCP session
- **WHEN** OpenCode submits a valid `initialize` JSON-RPC request to `http://esp32-mcp.local/mcp`
- **THEN** the endpoint SHALL return a valid MCP initialization response that advertises tool support

#### Scenario: Client lists the available tools
- **WHEN** an initialized MCP client submits `tools/list`
- **THEN** the response SHALL list `system.status`, `gpio.read`, and `gpio.write` with their input schemas

#### Scenario: Client sends an invalid MCP request
- **WHEN** a client sends malformed JSON-RPC, an unsupported method, or an unknown tool to `/mcp`
- **THEN** the endpoint SHALL return a JSON-RPC error response without restarting the device

### Requirement: Device status tool
The MCP server SHALL provide a `system.status` tool that returns firmware version, WiFi connection state, assigned IP address when connected, mDNS hostname, uptime, and available heap.

#### Scenario: Client requests device status
- **WHEN** an MCP client calls `system.status`
- **THEN** the result SHALL contain the current firmware version, WiFi state, mDNS hostname, uptime, and available heap

### Requirement: Restricted onboard LED control
The MCP server SHALL provide `gpio.write` and `gpio.read` tools exclusively for GPIO 2, the DevKit V1 onboard LED. On startup, the firmware SHALL configure GPIO 2 as an output. `gpio.write` SHALL accept a Boolean level and set the LED output accordingly; `gpio.read` SHALL report the current GPIO 2 level. Both tools SHALL reject a requested pin other than 2 without changing any GPIO state.

#### Scenario: Client turns on the onboard LED
- **WHEN** an MCP client calls `gpio.write` with `pin` equal to 2 and `level` equal to true
- **THEN** the server SHALL set GPIO 2 high and return a successful tool result identifying the resulting level

#### Scenario: Client reads the onboard LED level
- **WHEN** an MCP client calls `gpio.read` with `pin` equal to 2
- **THEN** the server SHALL return the current Boolean level of GPIO 2

#### Scenario: Client requests an unsupported GPIO pin
- **WHEN** an MCP client calls `gpio.read` or `gpio.write` with a pin value other than 2
- **THEN** the server SHALL return an MCP tool error and SHALL not access or reconfigure that pin

### Requirement: Trusted-LAN-only deployment boundary
The system SHALL expose its MCP endpoint using plain HTTP without TLS or authentication. The project documentation and local configuration template SHALL state that this deployment is restricted to a trusted development LAN.

#### Scenario: Client reaches the endpoint without credentials
- **WHEN** a client on the same trusted development LAN sends a valid MCP request to `/mcp`
- **THEN** the server SHALL process the request without requiring credentials

### Requirement: Read-only HTTP status dashboard
The system SHALL serve a browser-readable status dashboard at `GET /` and a JSON status snapshot at `GET /api/status` on its existing HTTP server. Both endpoints SHALL be read-only and SHALL expose the current firmware version, WiFi connection state, assigned IP address when connected, mDNS hostname, uptime, available heap, and GPIO inventory.

#### Scenario: Browser opens the dashboard
- **WHEN** a browser requests `GET /` from a reachable device
- **THEN** the device SHALL return an HTML status dashboard that displays device status and GPIO inventory

#### Scenario: Client requests the status snapshot
- **WHEN** a client requests `GET /api/status` from a reachable device
- **THEN** the device SHALL return a JSON document containing device status and GPIO inventory

#### Scenario: Client attempts to control hardware through the status routes
- **WHEN** a client sends a non-GET request to `/` or `/api/status`
- **THEN** the device SHALL not change GPIO configuration or GPIO levels through those routes

### Requirement: GPIO inventory classification and state
The JSON status snapshot and dashboard SHALL represent every valid GPIO number on the ESP32 DevKit V1. Each entry SHALL report the pin number, classification, direction, readable status, writable status, firmware-management status, and a level when the pin is safe to sample. Pins unavailable because they are connected to flash SHALL be classified as `reserved` with no sampled level. Input-only pins, bootstrapping pins, and UART pins SHALL be visibly classified. GPIO 2 SHALL be classified as the onboard LED, managed by firmware, and writable; all other pins SHALL be reported as not writable through the exposed interfaces.

#### Scenario: Client inspects the managed onboard LED
- **WHEN** a client reads the status snapshot while the firmware is running
- **THEN** the GPIO 2 entry SHALL identify an output-level state, `onboard-led` classification, and writable/managed status

#### Scenario: Client inspects a flash-connected GPIO
- **WHEN** a client reads the status snapshot
- **THEN** every flash-connected GPIO entry SHALL be classified as `reserved`, report no sampled level, and report not readable or writable

#### Scenario: Client inspects input-only GPIOs
- **WHEN** a client reads the status snapshot
- **THEN** GPIOs 34 through 39 SHALL be classified as `input-only` and report not writable status

### Requirement: Automatic dashboard refresh
The dashboard SHALL request `/api/status` automatically at least once every two seconds and update its displayed device and GPIO values without requiring a full page reload. If a refresh request fails, the dashboard SHALL retain the last successful values and visibly indicate that the latest refresh failed.

#### Scenario: Device state changes while the dashboard is open
- **WHEN** a displayed status value changes and the dashboard remains open
- **THEN** the dashboard SHALL show the updated value after its next periodic status request without a full page reload

#### Scenario: Dashboard refresh is temporarily unavailable
- **WHEN** a periodic request to `/api/status` fails after a previous successful request
- **THEN** the dashboard SHALL keep the previous values visible and show a refresh failure indicator
