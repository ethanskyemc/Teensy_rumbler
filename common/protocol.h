#pragma once

// Deterministic framing for ESP-NOW and UART. Both transports carry the same
// bytes so the C3 can validate a frame and forward it unchanged.
//
//   SYNC1 SYNC2 LEN TYPE PAYLOAD CRC16_LE
//
// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, xorout 0)
// covers LEN, TYPE, and PAYLOAD. Sync bytes are not covered.
// Multi-byte integers are little-endian. There is no struct dump on the wire.

#include "commands.h"

#include <stddef.h>
#include <stdint.h>

namespace siren {

constexpr uint8_t kFrameOverhead = 6;
constexpr uint8_t kMaxFrameBytes = kMaxPayload + kFrameOverhead;

inline uint16_t crc16CcittFalse(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000u) != 0) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021u);
            } else {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

namespace detail {

inline void writeU16(uint8_t* dest, uint16_t value) {
    dest[0] = static_cast<uint8_t>(value & 0xFFu);
    dest[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

inline uint16_t readU16(const uint8_t* src) {
    return static_cast<uint16_t>(src[0] | (static_cast<uint16_t>(src[1]) << 8));
}

inline void writeI16(uint8_t* dest, int16_t value) {
    writeU16(dest, static_cast<uint16_t>(value));
}

inline int16_t readI16(const uint8_t* src) {
    return static_cast<int16_t>(readU16(src));
}

}  // namespace detail

inline bool serializeCommand(const CommandPacket& in, uint8_t* out, size_t out_len) {
    if (out == 0 || out_len < kCommandWireSize) {
        return false;
    }
    out[0] = in.version;
    out[1] = in.epoch;
    out[2] = static_cast<uint8_t>(in.command);
    out[3] = static_cast<uint8_t>(in.sound);
    out[4] = static_cast<uint8_t>(in.param);
    detail::writeI16(&out[5], in.value);
    detail::writeU16(&out[7], in.sequence);
    return true;
}

inline bool deserializeCommand(const uint8_t* in, size_t in_len, CommandPacket& out) {
    if (in == 0 || in_len != kCommandWireSize) {
        return false;
    }
    if (!commandParamValid(in[4]) || !soundIdValid(static_cast<SoundID>(in[3]))) {
        return false;
    }
    const uint8_t command = in[2];
    if (command > static_cast<uint8_t>(CommandType::REQUEST_STATUS)) {
        return false;
    }
    out.version = in[0];
    out.epoch = in[1];
    out.command = static_cast<CommandType>(command);
    out.sound = static_cast<SoundID>(in[3]);
    out.param = static_cast<CommandParam>(in[4]);
    out.value = detail::readI16(&in[5]);
    out.sequence = detail::readU16(&in[7]);
    return true;
}

inline bool serializeStatus(const StatusPacket& in, uint8_t* out, size_t out_len) {
    if (out == 0 || out_len < kStatusWireSize) {
        return false;
    }
    out[0] = in.version;
    out[1] = in.epoch;
    out[2] = static_cast<uint8_t>(in.sound);
    out[3] = in.playing ? 1u : 0u;
    out[4] = in.rumbler_enabled ? 1u : 0u;
    out[5] = in.volume_master;
    out[6] = in.volume_siren;
    out[7] = in.volume_rumbler;
    out[8] = static_cast<uint8_t>(in.teensy);
    out[9] = static_cast<uint8_t>(in.sd);
    out[10] = in.flags;
    out[11] = in.remote_epoch;
    detail::writeU16(&out[12], in.ack_sequence);
    detail::writeU16(&out[14], in.status_sequence);
    return true;
}

inline bool deserializeStatus(const uint8_t* in, size_t in_len, StatusPacket& out) {
    if (in == 0 || in_len != kStatusWireSize) {
        return false;
    }
    if (in[3] > 1u || in[4] > 1u) {
        return false;
    }
    if (!soundIdValid(static_cast<SoundID>(in[2])) || !teensyStatusValid(in[8]) ||
        !sdStatusValid(in[9])) {
        return false;
    }
    out.version = in[0];
    out.epoch = in[1];
    out.sound = static_cast<SoundID>(in[2]);
    out.playing = in[3] == 1u;
    out.rumbler_enabled = in[4] == 1u;
    out.volume_master = in[5];
    out.volume_siren = in[6];
    out.volume_rumbler = in[7];
    out.teensy = static_cast<TeensyStatus>(in[8]);
    out.sd = static_cast<SdStatus>(in[9]);
    out.flags = in[10];
    out.remote_epoch = in[11];
    out.ack_sequence = detail::readU16(&in[12]);
    out.status_sequence = detail::readU16(&in[14]);
    return true;
}

// Writes one frame. Returns 0 when the buffer is too small or the payload
// is empty or larger than kMaxPayload.
inline size_t encodeFrame(PacketType type, const uint8_t* payload, uint8_t payload_len,
                          uint8_t* out, size_t out_cap) {
    if (out == 0 || payload == 0 || payload_len == 0 || payload_len > kMaxPayload) {
        return 0;
    }
    const size_t total = static_cast<size_t>(kFrameOverhead) + payload_len;
    if (out_cap < total) {
        return 0;
    }
    out[0] = kUartSync1;
    out[1] = kUartSync2;
    out[2] = payload_len;
    out[3] = static_cast<uint8_t>(type);
    for (uint8_t i = 0; i < payload_len; ++i) {
        out[4 + i] = payload[i];
    }
    const uint16_t crc = crc16CcittFalse(&out[2], static_cast<size_t>(2u + payload_len));
    out[4 + payload_len] = static_cast<uint8_t>(crc & 0xFFu);
    out[5 + payload_len] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
    return total;
}

inline size_t buildCommandFrame(const CommandPacket& cmd, uint8_t* out, size_t out_cap) {
    uint8_t payload[kCommandWireSize];
    if (!serializeCommand(cmd, payload, sizeof(payload))) {
        return 0;
    }
    return encodeFrame(PacketType::COMMAND, payload, kCommandWireSize, out, out_cap);
}

inline size_t buildStatusFrame(const StatusPacket& status, uint8_t* out, size_t out_cap) {
    uint8_t payload[kStatusWireSize];
    if (!serializeStatus(status, payload, sizeof(payload))) {
        return 0;
    }
    return encodeFrame(PacketType::STATUS, payload, kStatusWireSize, out, out_cap);
}

// Byte-at-a-time frame finder. Pointers from payload() and frame() are valid
// only until the next push(). On a bad CRC the parser hunts for a new sync
// inside the rejected bytes, then continues.
class FrameParser {
public:
    FrameParser() { reset(); }

    void reset() {
        state_ = State::SYNC1;
        index_ = 0;
        expected_len_ = 0;
        frame_len_ = 0;
        type_ = PacketType::COMMAND;
        last_byte_ms_ = 0;
        have_time_ = false;
        recover_depth_ = 0;
    }

    bool push(uint8_t byte, uint32_t now_ms) {
        if (have_time_ && state_ != State::SYNC1 &&
            elapsedMs(now_ms, last_byte_ms_, kUartInterByteTimeoutMs)) {
            const bool keep_time = have_time_;
            const uint32_t keep_ms = last_byte_ms_;
            reset();
            have_time_ = keep_time;
            last_byte_ms_ = keep_ms;
            state_ = State::SYNC1;
        }
        have_time_ = true;
        last_byte_ms_ = now_ms;
        return consume(byte);
    }

    PacketType type() const { return type_; }
    const uint8_t* payload() const { return &buf_[4]; }
    uint8_t payloadLength() const { return expected_len_; }
    const uint8_t* frame() const { return buf_; }
    uint8_t frameLength() const { return frame_len_; }

private:
    enum class State : uint8_t {
        SYNC1,
        SYNC2,
        LEN,
        TYPE,
        PAYLOAD,
        CRC_LO,
        CRC_HI,
    };

    bool consume(uint8_t byte) {
        switch (state_) {
            case State::SYNC1:
                if (byte == kUartSync1) {
                    buf_[0] = byte;
                    index_ = 1;
                    state_ = State::SYNC2;
                }
                return false;
            case State::SYNC2:
                if (byte == kUartSync2) {
                    buf_[1] = byte;
                    index_ = 2;
                    state_ = State::LEN;
                    return false;
                }
                state_ = State::SYNC1;
                if (byte == kUartSync1) {
                    buf_[0] = byte;
                    index_ = 1;
                    state_ = State::SYNC2;
                }
                return false;
            case State::LEN:
                if (byte == 0 || byte > kMaxPayload) {
                    state_ = State::SYNC1;
                    index_ = 0;
                    if (byte == kUartSync1) {
                        buf_[0] = byte;
                        index_ = 1;
                        state_ = State::SYNC2;
                    }
                    return false;
                }
                buf_[2] = byte;
                expected_len_ = byte;
                index_ = 3;
                state_ = State::TYPE;
                return false;
            case State::TYPE:
                buf_[3] = byte;
                index_ = 4;
                state_ = State::PAYLOAD;
                return false;
            case State::PAYLOAD:
                buf_[index_] = byte;
                index_ = static_cast<uint8_t>(index_ + 1u);
                if (index_ >= static_cast<uint8_t>(4u + expected_len_)) {
                    state_ = State::CRC_LO;
                }
                return false;
            case State::CRC_LO:
                buf_[index_] = byte;
                index_ = static_cast<uint8_t>(index_ + 1u);
                state_ = State::CRC_HI;
                return false;
            case State::CRC_HI:
                buf_[index_] = byte;
                index_ = static_cast<uint8_t>(index_ + 1u);
                return finish();
        }
        state_ = State::SYNC1;
        return false;
    }

    bool finish() {
        const uint8_t len = expected_len_;
        const uint8_t total = static_cast<uint8_t>(kFrameOverhead + len);
        const uint16_t got = detail::readU16(&buf_[4 + len]);
        const uint16_t calc = crc16CcittFalse(&buf_[2], static_cast<size_t>(2u + len));
        if (got == calc && packetTypeValid(buf_[3])) {
            frame_len_ = total;
            type_ = static_cast<PacketType>(buf_[3]);
            state_ = State::SYNC1;
            index_ = 0;
            recover_depth_ = 0;
            return true;
        }

        if (recover_depth_ >= 4) {
            state_ = State::SYNC1;
            index_ = 0;
            recover_depth_ = 0;
            return false;
        }

        uint8_t copy[kMaxFrameBytes];
        for (uint8_t i = 0; i < total; ++i) {
            copy[i] = buf_[i];
        }
        state_ = State::SYNC1;
        index_ = 0;
        expected_len_ = 0;
        recover_depth_ = static_cast<uint8_t>(recover_depth_ + 1u);
        bool found = false;
        for (uint8_t i = 1; i < total; ++i) {
            if (consume(copy[i])) {
                found = true;
                break;
            }
        }
        if (!found) {
            recover_depth_ = 0;
        }
        return found;
    }

    State state_;
    uint8_t buf_[kMaxFrameBytes];
    uint8_t index_;
    uint8_t expected_len_;
    uint8_t frame_len_;
    PacketType type_;
    uint32_t last_byte_ms_;
    bool have_time_;
    uint8_t recover_depth_;
};

inline bool protocolSelfTest() {
    const uint8_t crc_vector[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    if (crc16CcittFalse(crc_vector, sizeof(crc_vector)) != 0x29B1u) {
        return false;
    }

    if (nextSequence(0) != 1 || nextSequence(1) != 2 || nextSequence(65535) != 1) {
        return false;
    }
    if (!sequenceIsNewer(1, 0) || sequenceIsNewer(1, 1) || !sequenceIsNewer(1, 65535)) {
        return false;
    }

    const CommandPacket play = makePlay(7, 0x1234, SoundID::WAIL, CommandParam::PRESS);
    uint8_t raw[kCommandWireSize];
    if (!serializeCommand(play, raw, sizeof(raw))) {
        return false;
    }
    const uint8_t expect_cmd[] = {0x01, 0x07, 0x01, 0x01, 0x01, 0x00, 0x00, 0x34, 0x12};
    for (uint8_t i = 0; i < kCommandWireSize; ++i) {
        if (raw[i] != expect_cmd[i]) {
            return false;
        }
    }
    CommandPacket decoded = {};
    if (!deserializeCommand(raw, sizeof(raw), decoded) || !commandFieldsValid(decoded)) {
        return false;
    }
    if (decoded.epoch != 7 || decoded.command != CommandType::PLAY ||
        decoded.sound != SoundID::WAIL || decoded.param != CommandParam::PRESS ||
        decoded.value != 0 || decoded.sequence != 0x1234) {
        return false;
    }
    if (deserializeCommand(raw, sizeof(raw) - 1, decoded)) {
        return false;
    }

    CommandPacket bad = play;
    bad.version = 0;
    if (commandFieldsValid(bad)) {
        return false;
    }
    bad = play;
    bad.sound = SoundID::NONE;
    if (commandFieldsValid(bad)) {
        return false;
    }

    StatusPacket status = {};
    status.version = kProtocolVersion;
    status.epoch = 3;
    status.sound = SoundID::YELP;
    status.playing = true;
    status.rumbler_enabled = false;
    status.volume_master = 30;
    status.volume_siren = 50;
    status.volume_rumbler = 30;
    status.teensy = TeensyStatus::BOOT;
    status.sd = SdStatus::UNKNOWN;
    status.flags = 0;
    status.remote_epoch = 7;
    status.ack_sequence = 2;
    status.status_sequence = 9;
    uint8_t status_raw[kStatusWireSize];
    if (!serializeStatus(status, status_raw, sizeof(status_raw))) {
        return false;
    }
    const uint8_t expect_status[] = {0x01, 0x03, 0x02, 0x01, 0x00, 0x1E, 0x32, 0x1E,
                                     0x01, 0x00, 0x00, 0x07, 0x02, 0x00, 0x09, 0x00};
    for (uint8_t i = 0; i < kStatusWireSize; ++i) {
        if (status_raw[i] != expect_status[i]) {
            return false;
        }
    }
    StatusPacket status_out = {};
    if (!deserializeStatus(status_raw, sizeof(status_raw), status_out) ||
        !statusFieldsValid(status_out) || status_out.sound != SoundID::YELP ||
        !status_out.playing || status_out.rumbler_enabled || status_out.ack_sequence != 2) {
        return false;
    }

    uint8_t frame[kMaxFrameBytes];
    const size_t n = buildCommandFrame(play, frame, sizeof(frame));
    if (n != static_cast<size_t>(kFrameOverhead + kCommandWireSize)) {
        return false;
    }

    FrameParser parser;
    bool saw = false;
    for (size_t i = 0; i < n; ++i) {
        if (parser.push(frame[i], 1000)) {
            saw = true;
        }
    }
    if (!saw || parser.type() != PacketType::COMMAND || parser.payloadLength() != kCommandWireSize) {
        return false;
    }
    if (!deserializeCommand(parser.payload(), parser.payloadLength(), decoded) ||
        decoded.sequence != 0x1234) {
        return false;
    }

    uint8_t corrupt[kMaxFrameBytes];
    for (size_t i = 0; i < n; ++i) {
        corrupt[i] = frame[i];
    }
    corrupt[n - 1] ^= 0xFFu;
    FrameParser reject;
    bool accepted_bad = false;
    for (size_t i = 0; i < n; ++i) {
        if (reject.push(corrupt[i], 2000)) {
            accepted_bad = true;
        }
    }
    if (accepted_bad) {
        return false;
    }
    bool accepted_after = false;
    for (size_t i = 0; i < n; ++i) {
        if (reject.push(frame[i], 2000)) {
            accepted_after = true;
        }
    }
    if (!accepted_after) {
        return false;
    }

    // A gap longer than the inter-byte timeout must abandon a partial header.
    // The bytes below would complete that header if it were still open.
    const uint8_t stale_crc_src[] = {0x01, 0x01, 0x42};
    const uint16_t stale_crc = crc16CcittFalse(stale_crc_src, sizeof(stale_crc_src));
    const uint8_t stale_rest[] = {
        0x01,
        0x42,
        static_cast<uint8_t>(stale_crc & 0xFFu),
        static_cast<uint8_t>((stale_crc >> 8) & 0xFFu),
    };
    FrameParser timed;
    if (timed.push(kUartSync1, 0) || timed.push(kUartSync2, 0) || timed.push(0x01, 0)) {
        return false;
    }
    bool finished_stale = false;
    for (size_t i = 0; i < sizeof(stale_rest); ++i) {
        if (timed.push(stale_rest[i], 1000)) {
            finished_stale = true;
        }
    }
    if (finished_stale) {
        return false;
    }
    bool recovered = false;
    for (size_t i = 0; i < n; ++i) {
        if (timed.push(frame[i], 1000)) {
            recovered = true;
        }
    }
    if (!recovered) {
        return false;
    }

    FrameParser prefixed;
    if (prefixed.push(kUartSync1, 4000)) {
        return false;
    }
    bool prefixed_ok = false;
    for (size_t i = 0; i < n; ++i) {
        if (prefixed.push(frame[i], 4000)) {
            prefixed_ok = true;
        }
    }
    if (!prefixed_ok) {
        return false;
    }

    LinkGuard link = {};
    const CommandPacket early = makePlay(4, 1, SoundID::YELP, CommandParam::PRESS);
    if (admitCommand(link, early) != AdmitResult::REJECT_NEEDS_HEARTBEAT) {
        return false;
    }
    if (admitCommand(link, makeHeartbeat(4, 1)) != AdmitResult::ACCEPT) {
        return false;
    }
    if (admitCommand(link, makePlay(4, 2, SoundID::YELP, CommandParam::PRESS)) != AdmitResult::ACCEPT) {
        return false;
    }
    if (admitCommand(link, makePlay(4, 2, SoundID::YELP, CommandParam::PRESS)) != AdmitResult::REJECT_STALE) {
        return false;
    }
    if (admitCommand(link, makePlay(9, 1, SoundID::WAIL, CommandParam::PRESS)) != AdmitResult::REJECT_EPOCH) {
        return false;
    }
    if (admitCommand(link, makeHeartbeat(9, 1)) != AdmitResult::ACCEPT) {
        return false;
    }
    if (admitCommand(link, makeStop(9, 2)) != AdmitResult::ACCEPT) {
        return false;
    }
    if (!commandFieldsValid(makeFailsafeStop(9, 3))) {
        return false;
    }
    return true;
}

}  // namespace siren
