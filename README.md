# ESP32 MCP Development Supporter

An ESP32 DevKit V1 firmware project that exposes an MCP server directly to
OpenCode for observing and controlling runtime GPIO state.

## Security Boundary

The MCP endpoint is plain, unauthenticated HTTP. Any device on the same network
can call the available tools and operate accessible GPIOs. Use this firmware
**only on a trusted development LAN**. It is not suitable for shared or
production networks.

## Prerequisites

- PlatformIO Core 6 or the PlatformIO IDE extension
- An ESP32 DevKit V1 connected by USB
- Access to a trusted 2.4 GHz WiFi development network

## WiFi Configuration

The local WiFi file is ignored by Git.

1. Copy `include/wifi_config.example.h` to `include/wifi_config.h`.
2. Set `WIFI_SSID` and `WIFI_PASSWORD` to credentials for the trusted LAN.
3. Do not commit `include/wifi_config.h`.

The repository includes a build-only ignored `wifi_config.h` so a local compile
can start immediately. Replace it before uploading because those values cannot
connect to a real network. Removing the file causes the build to fail with an
explicit missing-configuration diagnostic.

## Build and Upload

Build the firmware:

```powershell
pio run
```

Upload it to the connected DevKit V1:

```powershell
pio run --target upload
```

View serial diagnostics:

```powershell
pio device monitor --baud 115200
```

After WiFi connects, the serial output reports its assigned address, registers
`esp32-mcp.local` through mDNS, and starts the MCP service at:

```text
http://esp32-mcp.local/mcp
```

If the local network does not resolve mDNS, use the IP address printed in the
serial log temporarily.

## HTTP Status Dashboard

Open the following URL from a browser on the trusted development LAN:

```text
http://esp32-mcp.local/
```

The dashboard refreshes automatically every two seconds and shows firmware,
network, memory, and GPIO inventory state. It uses one GPIO card column on a
narrow display and two columns on a wide display. Accessible GPIO cards can
configure a pin or set an output level; the page warns before changing a
bootstrapping pin. Its machine-readable counterpart is:

```text
http://esp32-mcp.local/api/status
```

The status routes remain read-only. Dashboard controls use unauthenticated
`POST /api/gpio/configure` and `POST /api/gpio/write` routes on the same trusted
LAN boundary as MCP. GPIO 6-11 are flash-reserved and GPIO 1 and 3 are
UART-reserved; all are fully unavailable through MCP and the dashboard. GPIO
34-39 are input-only and accept only `pull: "none"`. Bootstrapping pins (0, 2,
4, 5, 12, and 15) are controllable but can affect a later boot.

At every firmware boot, every accessible GPIO starts as `input` with
`pull: "none"`. Runtime configuration is not persisted across reset or power
loss. A readable unconnected input can float high or low.

## OpenCode Configuration

Add the device to the developer's OpenCode configuration after the board joins
the network:

```json
{
  "mcp": {
    "esp32": {
      "type": "remote",
      "url": "http://esp32-mcp.local/mcp",
      "enabled": true,
      "oauth": false
    }
  }
}
```

Restart OpenCode or reload its configuration, then use the `esp32` tools.

## Available Tools

| Tool | Arguments | Result |
| --- | --- | --- |
| `system.status` | `{}` | Firmware version, WiFi state, IP, hostname, uptime, and free heap |
| `gpio.configure` | `{ "pin": 17, "mode": "input", "pull": "up" }` | Configures an input and its internal pull resistor |
| `gpio.configure` | `{ "pin": 17, "mode": "output", "initialLevel": false }` | Prepares the output level, then configures the output |
| `gpio.read` | `{ "pin": 17 }` | Current level, mode, pull, and access state |
| `gpio.write` | `{ "pin": 17, "level": true }` | Sets a GPIO already configured as an output |

GPIO 1, 3, and 6-11 are always rejected. GPIO 34-39 can only be configured as
inputs with no internal pull resistor and cannot be written. `gpio.write` never
changes a pin direction: configure the pin as output first. GPIO 2 is the
onboard LED, but follows this same flow and also starts as input/no-pull.

## MCP Smoke Test

After the device is reachable, call these MCP methods in order:

1. `initialize`
2. `tools/list`
3. `tools/call` with `system.status`
4. `tools/call` with `gpio.configure`, `pin: 2`, `mode: "output"`, and `initialLevel: false`
5. `tools/call` with `gpio.write`, `pin: 2`, and `level: true`
6. `tools/call` with `gpio.read` and `pin: 2`
7. `tools/call` with `gpio.write`, `pin: 2`, and `level: false`

The onboard LED should visibly turn on and off, and `gpio.read` should match
the requested level.
