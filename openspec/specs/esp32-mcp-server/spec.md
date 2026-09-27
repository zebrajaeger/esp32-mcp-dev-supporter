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
- **THEN** the response SHALL list `system.status`, `gpio.configure`, `gpio.read`, and `gpio.write` with their input schemas

#### Scenario: Client sends an invalid MCP request
- **WHEN** a client sends malformed JSON-RPC, an unsupported method, or an unknown tool to `/mcp`
- **THEN** the endpoint SHALL return a JSON-RPC error response without restarting the device

### Requirement: Device status tool
The MCP server SHALL provide a `system.status` tool that returns firmware version, WiFi connection state, assigned IP address when connected, mDNS hostname, uptime, and available heap.

#### Scenario: Client requests device status
- **WHEN** an MCP client calls `system.status`
- **THEN** the result SHALL contain the current firmware version, WiFi state, mDNS hostname, uptime, and available heap

### Requirement: Restricted onboard LED control
The MCP server SHALL treat GPIO 2 as an accessible GPIO under the runtime GPIO configuration contract, rather than configuring it as an output on startup. GPIO 2 SHALL start as an input with no internal pull resistor, SHALL require `gpio.configure` before `gpio.write`, and SHALL follow the same read, configuration, write, reset, and bootstrapping-warning behavior as the other accessible bootstrapping pins.

#### Scenario: Client controls the onboard LED after configuration
- **WHEN** an MCP client configures GPIO 2 as an output with an initial level and then calls `gpio.write` with `pin` equal to 2
- **THEN** the server SHALL set GPIO 2 to the requested level and return a successful tool result identifying the resulting level

#### Scenario: Client writes the onboard LED before configuration
- **WHEN** an MCP client calls `gpio.write` with `pin` equal to 2 before configuring it as an output
- **THEN** the server SHALL return an MCP tool error and SHALL not change GPIO 2

#### Scenario: Client turns on the onboard LED
- **WHEN** an MCP client configures GPIO 2 as output and calls `gpio.write` with `pin` equal to 2 and `level` equal to true
- **THEN** the server SHALL set GPIO 2 high and return a successful tool result identifying the resulting level

#### Scenario: Client reads the onboard LED level
- **WHEN** an MCP client calls `gpio.read` with `pin` equal to 2
- **THEN** the server SHALL return the current GPIO 2 level and runtime configuration

#### Scenario: Client requests an unsupported GPIO pin
- **WHEN** an MCP client calls `gpio.read`, `gpio.configure`, or `gpio.write` with a pin value that is invalid, UART-reserved, or flash-reserved
- **THEN** the server SHALL return an MCP tool error and SHALL not access or reconfigure that pin

### Requirement: Trusted-LAN-only deployment boundary
The system SHALL expose its MCP and browser GPIO control endpoints using plain HTTP without TLS or authentication. The project documentation and local configuration template SHALL state that this deployment is restricted to a trusted development LAN.

#### Scenario: Client reaches a control endpoint without credentials
- **WHEN** a client on the same trusted development LAN sends a valid MCP or browser GPIO control request
- **THEN** the server SHALL process the request without requiring credentials

#### Scenario: Client reaches the endpoint without credentials
- **WHEN** a client on the same trusted development LAN sends a valid MCP request to `/mcp`
- **THEN** the server SHALL process the request without requiring credentials

### Requirement: Read-only HTTP status dashboard
The system SHALL serve a browser-readable status dashboard at `GET /` and a JSON status snapshot at `GET /api/status` on its existing HTTP server. Both endpoints SHALL remain read-only and SHALL expose the current firmware version, WiFi connection state, assigned IP address when connected, mDNS hostname, uptime, available heap, and GPIO inventory. The dashboard SHALL use a responsive GPIO card layout that displays one column on narrow viewports and two columns when the viewport is sufficiently wide.

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
The JSON status snapshot and dashboard SHALL represent every valid GPIO number on the ESP32 DevKit V1. Each entry SHALL report the pin number, classification, direction, readable status, writable status, firmware-management status, runtime mode, runtime pull configuration, and a level when the pin is accessible for sampling. GPIO 1 and 3 SHALL be classified as `uart`, GPIO 6 through 11 as `reserved`, and each of those entries SHALL expose neither a sampled level nor any GPIO control capability. GPIO 34 through 39 SHALL be classified as `input-only` and report their input/no-pull constraint. GPIO 2 SHALL be classified as the onboard LED but SHALL not be writable until configured as an output.

#### Scenario: Client inspects the managed onboard LED
- **WHEN** a client reads the status snapshot while the firmware is running
- **THEN** the GPIO 2 entry SHALL identify its `onboard-led` classification and its current input or output runtime state

#### Scenario: Client inspects a flash-connected GPIO
- **WHEN** a client reads the status snapshot
- **THEN** every flash-connected GPIO entry SHALL be classified as `reserved`, report no sampled level, and report not readable or writable

#### Scenario: Client inspects input-only GPIOs
- **WHEN** a client reads the status snapshot
- **THEN** GPIOs 34 through 39 SHALL be classified as `input-only`, report their input/no-pull runtime state, and report not writable status

#### Scenario: Client inspects UART GPIOs
- **WHEN** a client reads the status snapshot
- **THEN** GPIO 1 and 3 SHALL be classified as `uart`, report no sampled level, and report no readable or writable capability

### Requirement: Runtime GPIO configuration and control
The system SHALL expose `gpio.configure`, `gpio.read`, and `gpio.write` MCP tools for every valid GPIO except GPIO 1 and 3, which are reserved for UART, and GPIO 6 through 11, which are reserved for flash. At each firmware boot, every accessible GPIO SHALL be configured as an input with no internal pull resistor and its configuration SHALL exist only until reset or power loss. `gpio.read` SHALL return the current level and runtime configuration of an accessible GPIO. `gpio.configure` SHALL accept either an input mode with a required `pull` value of `none`, `up`, or `down`, or an output mode with a required Boolean `initialLevel`; it SHALL prepare the requested output level before enabling output mode. `gpio.write` SHALL accept a Boolean level only for an accessible GPIO currently configured as an output. GPIO 34 through 39 SHALL accept only input mode with `pull` set to `none`.

#### Scenario: Client configures and writes a general-purpose GPIO
- **WHEN** an MCP client configures an accessible general-purpose GPIO as output with an `initialLevel` and then calls `gpio.write` for that pin
- **THEN** the server SHALL return successful results for both requests and the subsequent `gpio.read` result SHALL report the written level and output configuration

#### Scenario: Client configures an input with a pull resistor
- **WHEN** an MCP client configures an accessible GPIO as input with `pull` set to `up` or `down`
- **THEN** the server SHALL apply the selected input configuration and report it through `gpio.read` and the HTTP status snapshot

#### Scenario: Client writes before output configuration
- **WHEN** an MCP client calls `gpio.write` for an accessible GPIO that is not currently configured as an output
- **THEN** the server SHALL return an MCP tool error and SHALL not change that GPIO configuration or level

#### Scenario: Client requests a prohibited or unsupported configuration
- **WHEN** an MCP client accesses GPIO 1, 3, or 6 through 11, requests output mode for GPIO 34 through 39, or requests a pull resistor for GPIO 34 through 39
- **THEN** the server SHALL return an MCP tool error and SHALL not access or reconfigure the requested pin

#### Scenario: Device restarts after runtime GPIO control
- **WHEN** the device resets or loses power after any runtime GPIO configuration or write
- **THEN** each accessible GPIO SHALL again start as an input with no internal pull resistor and no previous output configuration or level SHALL be restored

### Requirement: Browser GPIO control surface
The system SHALL provide browser-accessible GPIO configuration and write operations for the same accessible pins and with the same validation rules as the MCP GPIO tools. The dashboard SHALL present an explicit apply action before changing a GPIO configuration or output level, SHALL visibly disable controls for inaccessible pins, and SHALL show a warning before applying a change to a bootstrapping pin. The control routes SHALL remain unauthenticated plain HTTP for use on the trusted development LAN.

#### Scenario: Browser configures a writable GPIO
- **WHEN** a browser submits a valid configuration or output-level change for an accessible GPIO through the dashboard
- **THEN** the device SHALL apply the same resulting GPIO runtime state that the equivalent MCP operation would apply and the dashboard SHALL show that updated state after refresh

#### Scenario: Browser selects an inaccessible GPIO
- **WHEN** a browser views GPIO 1, 3, or 6 through 11 in the dashboard
- **THEN** the dashboard SHALL identify the pin as inaccessible and SHALL not offer an action that can configure, read, or write it

#### Scenario: Browser changes a bootstrapping GPIO
- **WHEN** a browser prepares to apply a configuration or output-level change to GPIO 0, 2, 4, 5, 12, or 15
- **THEN** the dashboard SHALL display a warning that the pin affects bootstrapping before the change is sent

### Requirement: Automatic dashboard refresh
The dashboard SHALL request `/api/status` automatically at least once every two seconds and update its displayed device and GPIO values without requiring a full page reload. If a refresh request fails, the dashboard SHALL retain the last successful values and visibly indicate that the latest refresh failed.

#### Scenario: Device state changes while the dashboard is open
- **WHEN** a displayed status value changes and the dashboard remains open
- **THEN** the dashboard SHALL show the updated value after its next periodic status request without a full page reload

#### Scenario: Dashboard refresh is temporarily unavailable
- **WHEN** a periodic request to `/api/status` fails after a previous successful request
- **THEN** the dashboard SHALL keep the previous values visible and show a refresh failure indicator
