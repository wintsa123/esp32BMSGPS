# JK BMS BLE Protocol

- JK BLE matching is GATT-based: discover service `0xFFE0`, then enumerate `0xFFE1` characteristics.
- Select the write handle from `WRITE`/`WRITE_NO_RSP` properties and the notification handle from `NOTIFY`/`INDICATE`; old modules may use one characteristic for both roles.
- The response frame is 300 bytes with checksum at offset 299. The supported layouts are `JK04`, `JK02_24S`, and `JK02_32S`.
- Hardware/software version strings from frame `0x03` are diagnostic metadata, not a protocol selector. Lock the protocol after the first uniquely valid cell-info frame and clear it on disconnect.
