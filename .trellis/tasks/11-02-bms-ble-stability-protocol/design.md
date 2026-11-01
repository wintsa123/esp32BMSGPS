# Design

Keep NimBLE transport in esp_bms_bms_ble.c and pure protocol parsing in existing protocol modules. Bound all receive buffers. ANT uses a streaming parser that consumes complete length-delimited segments and checks CRC before publishing a main frame. It must handle trailers inside payload, corrupt prefixes, split headers and multiple complete frames. Unknown but well-formed auxiliary segments are validated and skipped.

Preserve existing full telemetry semantics. Partial telemetry gains an explicit present_fields bitmask for scalar readings and a temperature group; zero is a valid present value. Missing groups preserve snapshot state. A present temperature group can invalidate previously populated sensors. Identity-only pages do not establish live telemetry or refresh its timeout.

ANT diagnostic fields are optional, validity-tagged where necessary, copied to snapshot and exposed in existing JSON status. No large UI dashboard is added. Shared telemetry header is owned by protocol implementer; coordinate its exact field names with projection implementer.

Transport records write properties, GATT establishment deadline and callback session ownership. CCCD writes always use acknowledged GATT writes. Poll writes use WRITE when available, otherwise WRITE_NO_RSP, except EM02 requires WRITE. Candidate descriptor search ends at the next characteristic declaration, not at the service end. ANT channels default=FFE1/FFE1, A=FFF3/FFF4, B=FFF5/FFF6, selected explicitly via persisted device configuration. Defaults preserve FFE1. Alternate availability must be property-checked; report unavailable selections rather than silently change it.

Existing other BMS protocols and local encrypted API/HID security are preserved. EM02 reference evidence is checked independently and never relaxes validation.
