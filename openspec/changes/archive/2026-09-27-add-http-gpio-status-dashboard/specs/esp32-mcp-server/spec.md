# Spec Delta

## ADDED Requirements

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
