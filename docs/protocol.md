# Command protocol

Version 1. Both ESP-NOW and UART carry the same frame. The C3 validates it and can forward those bytes unchanged. The format does not change if the baud rate changes.

Do not `memcpy` a C++ struct onto the wire. Compilers may insert padding, and the two architectures are not a reason to depend on that. `common/protocol.h` writes every byte explicitly. Multi-byte integers are little-endian.

## Frame

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | Sync `0xA5` |
| 1 | 1 | Sync `0x5A` |
| 2 | 1 | Payload length, 1..32 |
| 3 | 1 | Packet type |
| 4 | N | Payload |
| 4+N | 2 | CRC-16, little-endian |

Packet types: `1` command, `2` status.

CRC-16/CCITT-FALSE covers length, type, and payload. It does not cover the sync bytes. Polynomial `0x1021`, initial value `0xFFFF`, no reflection, xorout `0`. The check value of the ASCII string `123456789` is `0x29B1`.

The parser reads one byte at a time. A gap of more than 50 ms inside a frame drops that frame. A bad CRC, an illegal length, or an unknown type returns the parser to sync hunt, including a rescan of the rejected bytes. Callers must copy `payload()` before the next byte, because the next `push` reuses the buffer.

## Command payload (9 bytes)

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u8 | Protocol version (`1`) |
| 1 | u8 | Remote epoch, `1..255` |
| 2 | u8 | `CommandType` |
| 3 | u8 | `SoundID` |
| 4 | u8 | `CommandParam` |
| 5 | i16 | Value |
| 7 | u16 | Sequence, `1..65535` |

`CommandType`: `NONE=0`, `PLAY=1`, `STOP=2`, `SET_VOLUME=3`, `RUMBLER_ENABLE=4`, `RUMBLER_DISABLE=5`, `HEARTBEAT=6`, `REQUEST_STATUS=7`.

`SoundID`: `NONE=0`, `WAIL=1`, `YELP=2`, `HI_LO=3`, `PHASER=4`, `PIERCER=5`, `AIR_HORN=6`, `CUSTOM_1=7` through `CUSTOM_4=10`.

`CommandParam`: `NONE=0`, `PRESS=1`, `RELEASE=2`, `GAIN_MASTER=3`, `GAIN_SIREN=4`, `GAIN_RUMBLER=5`, `FAILSAFE=6`.

Legal combinations:

| Command | Sound | Param | Value |
| --- | --- | --- | --- |
| PLAY | not `NONE` | `PRESS` or `RELEASE` | 0 |
| STOP | `NONE` | `NONE` or `FAILSAFE` | 0 |
| SET_VOLUME | `NONE` | one `GAIN_*` | 0..100 |
| RUMBLER_ENABLE, RUMBLER_DISABLE, HEARTBEAT, REQUEST_STATUS | `NONE` | `NONE` | 0 |

`STOP` is ALL STOP. `FAILSAFE` marks a STOP built by the C3, not by a key.

PLAY/RELEASE stops a momentary sound (Air Horn). A RELEASE for a latched sound does not stop it. Latched sounds replace each other: Wail, then Yelp, starts Yelp immediately. Momentary vs latched is `soundIsMomentary()` in `common/sounds.h`.

`SET_VOLUME` is an absolute level in application units, not a step. The remote may send a step only after a fresh status packet has reported the current master volume. The step size is remote configuration (`kVolumeStep`, 5). Siren and Rumbler trims use the same command with `GAIN_SIREN` or `GAIN_RUMBLER`. There is no keypad binding for those trims yet.

Sequence `0` and epoch `0` are illegal. The remote starts at sequence 1. Each new heartbeat and each new user command increments it, skipping 0 after 65535. A retransmission uses the same sequence. `sequenceIsNewer()` treats equality as a replay and uses a half-range window so the counter can wrap.

## Who may change audio

`admitCommand()` is the shared gate.

- The first accepted packet for a link must be a heartbeat. It locks the epoch.
- A PLAY, STOP, volume change, or Rumbler command before that heartbeat is rejected.
- A different epoch is rejected unless the packet is a heartbeat, which then becomes the new lock. Packets from the old epoch die.
- A sequence that is not newer than the last accepted sequence is rejected.

Rejected packets do not change audio state. After a timeout, a PLAY that was already in flight is older than the failsafe STOP, or it belongs to an epoch that is no longer locked, so it cannot restart the siren.

The C3 heartbeat timer resets only on HEARTBEAT, not on other commands. If heartbeats stop for 1000 ms while audio is active, the C3 builds `makeFailsafeStop()` with the locked epoch and `nextSequence(last)`.

## Status payload (16 bytes)

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u8 | Protocol version |
| 1 | u8 | Teensy epoch, `1..255` |
| 2 | u8 | Selected sound |
| 3 | u8 | Playing, 0 or 1 |
| 4 | u8 | Rumbler enabled, 0 or 1 |
| 5 | u8 | Master volume 0..100 |
| 6 | u8 | Siren gain 0..100 |
| 7 | u8 | Rumbler gain 0..100 |
| 8 | u8 | `TeensyStatus` |
| 9 | u8 | `SdStatus` |
| 10 | u8 | Flags |
| 11 | u8 | Last remote epoch, 0 if none |
| 12 | u16 | Last accepted command sequence, 0 if none |
| 14 | u16 | Status sequence, `1..65535` |

`TeensyStatus`: `UNKNOWN=0`, `BOOT=1`, `READY=2`, `FAULT=3`.

`SdStatus`: `UNKNOWN=0`, `OK=1`, `NOT_PRESENT=2`, `MOUNT_FAILED=3`, `FILE_MISSING=4`.

Flags: bit 0 failsafe latched, bit 1 a mapped file was missing, bit 2 audio path initialized.

Playing with sound `NONE` is illegal. Stopped with a sound id is legal and means "selected, not playing". Phase 1 status is stopped, sound `NONE`, Teensy `BOOT`, SD `UNKNOWN`.

The remote treats a valid status packet newer than 1 second as the connection. Volume, Rumbler, and the playing tone on the display come from that packet.

## UART settings

115200 baud, 8 data bits, no parity, 1 stop bit, 3.3 V, common ground. Either side may move to a higher baud later without a packet-format change. ESP-NOW still wraps this same frame in its own radio FCS.
