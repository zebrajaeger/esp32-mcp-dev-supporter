# Tasks

## 1. Shared Status and GPIO Inventory

- [x] 1.1 Add an internal device-status snapshot builder shared by the MCP status path and HTTP status API; verify firmware version, WiFi state, IP, hostname, uptime, and free heap match the live device state.
- [x] 1.2 Define the ESP32 DevKit V1 GPIO inventory policy for valid GPIOs, flash-reserved pins, input-only pins, bootstrapping pins, UART pins, and the GPIO 2 onboard LED; verify reserved GPIOs 6 through 11 are never sampled and GPIOs 34 through 39 are classified input-only.
- [x] 1.3 Add live GPIO direction and safe level reporting, using the managed output state for GPIO 2 and `null` for unsampled reserved pins; verify GPIO 2 reports its commanded state and reserved pins report no level.
- [x] 1.4 Derive `readable`, `writable`, and `managed` fields from the GPIO policy so only GPIO 2 is writable and managed; verify every other inventory entry reports `writable: false`.

## 2. Read-Only HTTP Status Surface

- [x] 2.1 Register `GET /api/status` on the existing HTTP server and serialize the device snapshot plus complete GPIO inventory as JSON; verify a client receives all required fields and inventory entries.
- [x] 2.2 Register `GET /` to return a compact self-contained HTML dashboard shell; verify a browser receives a readable status page without external assets.
- [x] 2.3 Restrict dashboard routes to GET handlers and keep them independent of GPIO configuration and writes; verify non-GET requests cannot change GPIO 2 or any other GPIO state.

## 3. Auto-Refreshing Dashboard

- [x] 3.1 Add embedded browser JavaScript that requests `/api/status` on load and every two seconds, updating device values and the GPIO table without a full page reload; verify a changed GPIO 2 state appears after the next poll.
- [x] 3.2 Preserve the last successful dashboard values and display a refresh-failure indicator when a periodic status request fails; verify the indicator appears while previously rendered values remain visible.
- [x] 3.3 Render `true`, `false`, and `null` GPIO levels distinctly as high, low, and `n/a`, and surface classifications and access flags in the table; verify reserved pins cannot be confused with a low state.

## 4. Documentation and End-to-End Verification

- [x] 4.1 Update documentation with the dashboard and JSON URLs, automatic refresh behavior, GPIO classifications, and trusted-LAN warning; verify examples match the implemented endpoints.
- [x] 4.2 Build and upload the firmware to the ESP32 DevKit V1; verify serial output confirms WiFi, mDNS, and HTTP server startup and `pio run` succeeds.
- [x] 4.3 Validate `/api/status`, `GET /`, periodic refresh behavior, reserved/input-only/onboard-LED inventory classifications, and unchanged MCP GPIO 2 read/write behavior against the running device; verify the browser and API observations agree.
