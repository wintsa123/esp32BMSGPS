# Verified comparison evidence

maibms checkout /tmp/maibms-review-20261102 at 064f3aef02690b4941d5e19722f9de7dd41e8065 (MIT). Inspect Kotlin protocol/Frame.kt, RealtimeDecoder.kt, BitDict.kt, MosStateDict.kt, protocol/meter/LanBaoProtocol.kt and LuXingProtocol.kt; AndroidBleTransport.kt. Avoid verbatim nontrivial copies unless retaining its MIT notice.

Local subscription error returns without terminating while tick lacks establishment watchdog. Characteristic acceptance includes WRITE_NO_RSP but bms_write_frame always calls acknowledged write. ANT decode rejects compound real-time frames and transport consumes whole notification buffer as one frame. Bluebabe sets partial for both pages but snapshot projection clears measured current/SOC/capacity and unconditionally writes zero voltage from identity pages.

ANT primary segment 7E addr func regLo regHi len payload crcLE; auxiliary func regLo regHi len payload crcLE; final AA55 once. Each segment CRC excludes 7E and trailer. Current fixtures at tests/ant_bms_protocol_selftest.c; 152-byte single frame accepted, 304-byte two frames rejected, 158-byte frame with valid FE segment rejected before fix.

EM02 remote baseline 22cd1e33 has four Modbus requests and big endian response fields from /vol1/1000/project/_ref/motexa270-src/sources/v6/l0.java (source-known, no hardware confirmation). maibms LuXing uses little endian. Compare exact source, preserve identity gate, CRC, acknowledged writes and evidence limits.

Pre-existing dirty: hardware-build-flash.md, workspace wintsa journal/index, README.md/README.en.md, .reasonix tasks, archived October tasks and broad/pcb-revisions. Do not stage these as task work.
