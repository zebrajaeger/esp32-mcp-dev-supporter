# ESP32 MCP Development Supporter

An ESP32 DevKit V1 firmware project that exposes a minimal MCP server directly
to OpenCode. The first release controls only the onboard LED on GPIO 2.

## Security Boundary

The MCP endpoint is plain, unauthenticated HTTP. Any device on the same network
can call the available tools and operate GPIO 2. Use this firmware **only on a
trusted development LAN**. It is not suitable for shared or production networks.

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
| `gpio.read` | `{ "pin": 2 }` | Current Boolean GPIO 2 level |
| `gpio.write` | `{ "pin": 2, "level": true }` | Sets and returns GPIO 2 level |

GPIO values other than 2 are rejected. The firmware configures GPIO 2 as an
output after boot; it does not expose a GPIO configuration tool.

## MCP Smoke Test

After the device is reachable, call these MCP methods in order:

1. `initialize`
2. `tools/list`
3. `tools/call` with `system.status`
4. `tools/call` with `gpio.write`, `pin: 2`, and `level: true`
5. `tools/call` with `gpio.read` and `pin: 2`
6. `tools/call` with `gpio.write`, `pin: 2`, and `level: false`

The onboard LED should visibly turn on and off, and `gpio.read` should match
the requested level.
