# Design

## Context

The current firmware configures only GPIO 2 as an output, stores its commanded level, and exposes that state through fixed MCP schemas. `mcp_server.cpp` also embeds a status page whose device definition list and seven-column GPIO table are built directly from `/api/status`. See `proposal.md` for motivation and the spec delta for the behavioral contract.

## Goals / Non-Goals

**Goals:**

- Establish one GPIO runtime model that is authoritative for both MCP and browser operations.
- Make every allowed operation explicit, hardware-aware, and reset-safe.
- Preserve serial diagnostics by keeping UART GPIO 1 and 3 outside all GPIO service calls.
- Make the dashboard compact on wide screens without losing the complete GPIO inventory.

**Non-Goals:**

- Persisting configuration across reset, boot, or power loss.
- Configuring or exposing arbitrary peripheral functions such as PWM, ADC, interrupts, or I2C.
- Adding authentication, TLS, access control, or a public-network deployment mode.
- Supporting flash or UART pin access through an override.

## Decisions

### Centralize pin policy and runtime state in the GPIO service

The GPIO service will own a small per-pin runtime record containing accessibility, current mode, pull mode, and output eligibility. It will initialize every accessible valid GPIO to `input` and `none` at startup. MCP handlers, the status snapshot, and browser routes will query or mutate this service rather than duplicating validation.

This prevents the current mismatch where the dashboard samples broad GPIO state while MCP has a separate GPIO 2-only allowlist. It also makes an output-configured check possible before write operations. An alternative of retaining independent MCP and HTTP validation was rejected because policy changes could diverge and expose an unsafe pin through only one route.

### Use a capability matrix instead of a single allowlist

The service will classify valid GPIOs into four policy groups:

| Group | Pins | Read | Configure | Write |
| --- | --- | --- | --- | --- |
| inaccessible | 1, 3, 6-11 | no | no | no |
| input-only | 34-39 | yes | input/none only | no |
| bootstrapping | 0, 2, 4, 5, 12, 15 | yes | yes | after output config |
| general-purpose | remaining valid GPIOs | yes | yes | after output config |

Invalid ESP32 GPIO numbers remain absent from the inventory. The runtime model exposes classification and constraints for the UI, while policy enforcement stays on the device. Treating bootstrapping pins as inaccessible was rejected because the requested development workflow needs them, while allowing UART reads was rejected because the agreed contract is complete UART exclusion.

### Define explicit configuration transitions

Input configuration requires `pull` and sets direction plus pull state in one request. Output configuration requires `initialLevel`; implementation writes the output latch before switching direction to output, reducing a transient unintended output level. A write does not implicitly change direction or pull state.

The MCP schemas enforce these mutually exclusive parameter forms, and service validation remains the final authority. A single `gpio.write` that automatically selected output mode was rejected because it could turn an attached input circuit into a driven output without an explicit transition.

### Add narrow HTTP control routes that share MCP operations

The dashboard will use dedicated JSON `POST` routes for configuration and output writes, such as `/api/gpio/configure` and `/api/gpio/write`. Their request validation and response data will delegate to the same GPIO service functions as the MCP tools. `GET /` and `GET /api/status` remain read-only.

Posting JSON-RPC MCP envelopes from browser code was rejected because it couples the UI to MCP protocol details and complicates browser-side error handling. The additional routes inherit the existing trusted-LAN boundary and therefore introduce no separate security mechanism.

### Render the dashboard from GPIO status records

The status snapshot will provide sufficient per-pin state for the embedded page to render compact cards rather than a dense table. CSS grid will use one column by default and two columns at a desktop breakpoint. Each selectable, accessible pin has a detail/control form with explicit Apply; inaccessible pins show their reason without controls. Bootstrapping cards display a warning before the request is sent.

This keeps the HTML embedded in firmware and avoids a new static-asset pipeline. A fully separate web application was rejected because it increases deployment and memory complexity for a small LAN device UI.

## Risks / Trade-offs

- [Input pins with `pull: none` can float] -> Report actual sampled levels with the explicit mode/pull state and present the state in the dashboard so clients do not infer electrical certainty.
- [Changing bootstrapping pins can affect a later reset] -> Permit the requested runtime control but warn in the browser and expose their classification in MCP/status results.
- [Unauthenticated control exposes all accessible pins to the LAN] -> Preserve the documented trusted-development-LAN restriction and update the README to describe the enlarged control surface.
- [Broad GPIO initialization changes board electrical state at boot] -> Limit initialization to valid, non-UART, non-flash pins and use input with no pull, which avoids driving external circuits.
- [Embedded HTML becomes more complex] -> Keep the UI state entirely derived from the status and control JSON responses; no client-side persistence or framework is added.

## Migration Plan

1. Build and upload the firmware; existing clients that call the old GPIO 2-only `gpio.write` without prior configuration will receive a tool error.
2. Update OpenCode usage to configure GPIO 2 as output with an `initialLevel` before writing it.
3. Verify startup state, prohibited-pin rejection, input-only restrictions, MCP operations, HTTP control operations, and responsive dashboard rendering on the board.
4. Roll back by reflashing the prior firmware revision; no device-side migration or persisted state exists.
