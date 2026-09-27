# Tasks

## 1. GPIO Runtime Model

- [x] 1.1 Replace the GPIO 2-only service with per-pin policy and runtime state for valid ESP32 GPIOs, and verify GPIO 1, 3, and 6-11 are rejected without driver access.
- [x] 1.2 Initialize every accessible GPIO at boot as input with no pull resistor, and verify GPIO 34-39 remain input/no-pull while no GPIO is restored after reset.
- [x] 1.3 Implement explicit input and output configuration transitions, including output-latch initialization before output direction, and verify invalid pull, input-only, and output requests fail without state changes.
- [x] 1.4 Implement readable runtime snapshots and guarded writes that require prior output configuration, and verify read/configure/write state transitions on an allowed general-purpose GPIO and GPIO 2.

## 2. MCP GPIO Interface

- [x] 2.1 Replace fixed GPIO 2 tool schemas with general pin schemas and add `gpio.configure`, and verify `tools/list` advertises the four expected tools with valid input contracts.
- [x] 2.2 Route `gpio.read`, `gpio.configure`, and `gpio.write` through the GPIO service with structured successful results and tool errors, and verify allowed, inaccessible, input-only, and pre-configuration write cases through MCP requests.
- [x] 2.3 Extend `system.status` and the HTTP status snapshot GPIO records with runtime mode, pull, level, access, and writable state, and verify UART and flash pins expose no sampled level or control capability.

## 3. Browser Control Dashboard

- [x] 3.1 Add JSON HTTP configuration and write routes that share GPIO service validation with MCP, and verify valid dashboard requests update state while invalid or prohibited requests return errors without state changes.
- [x] 3.2 Replace the wide dashboard GPIO table with responsive one- and two-column GPIO cards plus a compact device-status area, and verify the rendered layout exposes every valid GPIO and remains usable at narrow viewport widths.
- [x] 3.3 Add explicit dashboard Apply controls, input pull selection, output initial-level and write controls, inaccessible-pin disabling, and bootstrapping warnings, and verify no browser interaction changes a GPIO before Apply or bypasses service validation.
- [x] 3.4 Preserve periodic status polling after control operations, and verify the dashboard reflects MCP-initiated and browser-initiated GPIO state changes without a page reload.

## 4. Documentation And Verification

- [x] 4.1 Update README MCP examples, dashboard guidance, pin restrictions, reset behavior, and trusted-LAN warning, and verify the documented GPIO 2 flow configures output before writing.
- [x] 4.2 Build the firmware with `pio run` and verify the build succeeds within the configured ESP32 flash limit.
- [x] 4.3 Upload to the DevKit V1 and perform end-to-end MCP and browser smoke tests for general-purpose, bootstrapping, input-only, UART, and flash-reserved GPIO cases.
