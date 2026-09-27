# Design

## Context

The existing port-80 ESP-IDF HTTP server accepts only `POST /mcp`; its `system.status` MCP tool already creates firmware and network status values. The GPIO service owns GPIO 2 as an output-only, firmware-managed onboard LED. See `proposal.md` for motivation and the capability specification delta for externally visible behavior.

The new HTTP surface must expose diagnostics without creating a second control plane. It must also distinguish safe observation from permissions: seeing a pin in the inventory does not make it writable through MCP or HTTP.

## Goals / Non-Goals

**Goals:**
- Provide browser and script access to one consistent device and GPIO status snapshot.
- Make ESP32 pin constraints visible rather than presenting every numbered pin as interchangeable.
- Refresh the browser view every two seconds with minimal device-side state and no framework dependency.
- Preserve the existing MCP and GPIO 2 control boundary.

**Non-Goals:**
- HTTP GPIO configuration, writing, or any browser control widgets.
- Expanding MCP writes or reads beyond the existing GPIO 2 scope.
- WebSockets, server-sent events, persistent browser sessions, or authentication.
- Electrical validation of unconnected input pins; sampled values may float.

## Decisions

### Generate one shared status snapshot

Introduce an internal status builder that produces device metadata and GPIO inventory once per request. The MCP `system.status` path may reuse the device portion later, while `GET /api/status` serializes the full snapshot as JSON. This prevents dashboard and API values from drifting while avoiding persistent caches that could become stale.

```text
                         +--> MCP system.status
                         |
status snapshot builder -+--> GET /api/status (JSON)
                         |
                         +--> GET / (HTML shell + browser refresh)
```

### Serve a static HTML shell plus a JSON refresh endpoint

`GET /` returns a compact self-contained HTML, CSS, and JavaScript document. The embedded script fetches `/api/status` immediately and then every 2,000 ms, replacing text and table rows in place. `GET /api/status` stays machine-readable and avoids coupling consumers to the presentation markup.

A full-page meta refresh was considered but rejected because it creates a disruptive browser experience and does not allow the page to preserve its last valid state on transient request failures. WebSockets and SSE were rejected because a two-second polling interval is sufficient for diagnostics and avoids connection/session complexity on the ESP32.

### Use an explicit GPIO inventory policy

Inventory entries include `pin`, `classification`, `direction`, `level`, `readable`, `writable`, and `managed`. The implementation uses a static per-pin policy table plus live direction/level information where sampling is safe.

```text
GPIO inventory entry
  pin:             physical ESP32 GPIO number
  classification:  general-purpose | onboard-led | input-only |
                   bootstrapping | uart | reserved
  direction:       input | output | unknown | reserved
  level:           true | false | null
  readable:        safe to sample through this firmware
  writable:        writable through exposed interfaces
  managed:         owned by the current firmware service
```

GPIOs 6 through 11 are marked `reserved` because they connect to flash and will not be sampled. GPIOs 34 through 39 are `input-only`. GPIOs 0, 2, 4, 5, 12, and 15 are marked `bootstrapping`; GPIO 2 is additionally identified as `onboard-led`. GPIOs 1 and 3 are marked `uart`. Other valid GPIOs are `general-purpose`. Unsupported numbering gaps are omitted rather than represented as hardware pins.

The existing firmware-managed output state remains the authoritative level for GPIO 2. Other readable pins use an instantaneous input sample, which can be electrically floating when no external circuit drives them.

### Enforce read-only HTTP routes by registration

Register only `HTTP_GET` handlers for `/` and `/api/status`. No HTTP handler will invoke GPIO configuration or `write_led`; unsupported methods retain the ESP-IDF server's normal not-found behavior. This is stronger and simpler than accepting a request and rejecting a write parameter after parsing.

## Risks / Trade-offs

- [Unconnected input pins may report unstable levels] -> Label values as sampled state, classify their direction, and do not imply that a low or high value represents a configured external signal.
- [The HTML payload and JSON inventory consume constrained firmware memory] -> Keep the page self-contained and compact, generate JSON only per request, and avoid external assets or frameworks.
- [A future GPIO expansion could accidentally make the dashboard permission fields stale] -> Derive `writable` and `managed` from the same GPIO policy used by hardware services.
- [HTTP remains unauthenticated in the trusted LAN] -> Keep the status routes read-only and preserve the existing deployment warning in documentation.
- [A failed poll can make diagnostics look frozen] -> Display a visible refresh-error state while retaining the last successful snapshot.

## Migration Plan

1. Add the status model and GPIO policy inventory while preserving existing MCP response behavior.
2. Register the two GET routes and build the firmware.
3. Upload to the DevKit V1 and verify JSON response, browser polling, pin classifications, and MCP GPIO 2 behavior.
4. Roll back by reflashing the previous firmware; no stored state, network configuration, or OpenCode configuration migration is required.
