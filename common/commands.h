#pragma once

// Shared command vocabulary and safety rules for the remote, the C3 bridge,
// and the Teensy. This header is portable C++: no Arduino types.
//
// Do not memcpy these structs onto UART or ESP-NOW. Field order here is not
// the wire format. Serialize with the functions in protocol.h.

#include <stdint.h>

namespace siren {

constexpr uint8_t kProtocolVersion = 1;

// Initial UART rate. The frame format does not depend on this value.
constexpr uint32_t kUartBaud = 115200;

// Remote transmit period, and how long a link may go without a heartbeat
// before playback must be forced silent.
constexpr uint32_t kHeartbeatIntervalMs = 250;
constexpr uint32_t kLinkTimeoutMs = 1000;

// A stalled UART byte stream is abandoned so the next sync word can be found.
constexpr uint32_t kUartInterByteTimeoutMs = 50;

// ESP-NOW channel both radios use. This is a shared configuration value,
// not a discovered network.
constexpr uint8_t kEspNowChannel = 1;

// Used when a config.h peer MAC is still all zeros. The bridge receives these
// frames without a paired peer. Paste real STA MACs to leave broadcast mode.
constexpr uint8_t kEspNowBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

inline bool macIsUnset(const uint8_t mac[6]) {
    for (uint8_t i = 0; i < 6; ++i) {
        if (mac[i] != 0) {
            return false;
        }
    }
    return true;
}

// Application volume units. 0 is mute, 100 is the full scale of our curve.
// The curve itself is owned by the Teensy audio engine.
constexpr int16_t kVolumeMin = 0;
constexpr int16_t kVolumeMax = 100;

constexpr uint8_t kUartSync1 = 0xA5;
constexpr uint8_t kUartSync2 = 0x5A;

constexpr uint8_t kMaxPayload = 32;

enum class CommandType : uint8_t {
    NONE = 0,
    PLAY = 1,
    STOP = 2,
    SET_VOLUME = 3,
    RUMBLER_ENABLE = 4,
    RUMBLER_DISABLE = 5,
    HEARTBEAT = 6,
    REQUEST_STATUS = 7,
};

enum class SoundID : uint8_t {
    NONE = 0,
    WAIL = 1,
    YELP = 2,
    HI_LO = 3,
    PHASER = 4,
    PIERCER = 5,
    AIR_HORN = 6,
    CUSTOM_1 = 7,
    CUSTOM_2 = 8,
    CUSTOM_3 = 9,
    CUSTOM_4 = 10,
};

// Meaning depends on CommandType. See commandFieldsValid().
enum class CommandParam : uint8_t {
    NONE = 0,
    PRESS = 1,
    RELEASE = 2,
    GAIN_MASTER = 3,
    GAIN_SIREN = 4,
    GAIN_RUMBLER = 5,
    FAILSAFE = 6,
};

enum class PacketType : uint8_t {
    COMMAND = 1,
    STATUS = 2,
};

enum class TeensyStatus : uint8_t {
    UNKNOWN = 0,
    BOOT = 1,
    READY = 2,
    FAULT = 3,
};

enum class SdStatus : uint8_t {
    UNKNOWN = 0,
    OK = 1,
    NOT_PRESENT = 2,
    MOUNT_FAILED = 3,
    FILE_MISSING = 4,
};

enum StatusFlag : uint8_t {
    STATUS_FLAG_FAILSAFE = 1u << 0,
    STATUS_FLAG_MISSING_FILE = 1u << 1,
    STATUS_FLAG_AUDIO_READY = 1u << 2,
};

// Logical command. Wire size is kCommandWireSize (9), which is not sizeof.
struct CommandPacket {
    uint8_t version;
    uint8_t epoch;
    CommandType command;
    SoundID sound;
    CommandParam param;
    int16_t value;
    uint16_t sequence;
};

// Teensy -> remote status. Wire size is kStatusWireSize (16).
struct StatusPacket {
    uint8_t version;
    uint8_t epoch;
    SoundID sound;
    bool playing;
    bool rumbler_enabled;
    uint8_t volume_master;
    uint8_t volume_siren;
    uint8_t volume_rumbler;
    TeensyStatus teensy;
    SdStatus sd;
    uint8_t flags;
    uint8_t remote_epoch;
    uint16_t ack_sequence;
    uint16_t status_sequence;
};

constexpr uint8_t kCommandWireSize = 9;
constexpr uint8_t kStatusWireSize = 16;

// Remembers which remote boot is allowed to change audio, and the last
// sequence that boot was allowed to execute. sequence 0 and epoch 0 are
// never valid. A heartbeat is the only command that may lock or replace
// an epoch, so a stale PLAY cannot reopen a siren after a reboot or a
// radio replay.
struct LinkGuard {
    uint8_t epoch;
    uint16_t last_sequence;
    bool heartbeat_seen;
};

enum class AdmitResult : uint8_t {
    ACCEPT = 0,
    REJECT_MALFORMED,
    REJECT_STALE,
    REJECT_EPOCH,
    REJECT_NEEDS_HEARTBEAT,
};

inline bool soundIdValid(SoundID id) {
    return static_cast<uint8_t>(id) <= static_cast<uint8_t>(SoundID::CUSTOM_4);
}

inline bool commandParamValid(uint8_t raw) {
    return raw <= static_cast<uint8_t>(CommandParam::FAILSAFE);
}

inline bool teensyStatusValid(uint8_t raw) {
    return raw <= static_cast<uint8_t>(TeensyStatus::FAULT);
}

inline bool sdStatusValid(uint8_t raw) {
    return raw <= static_cast<uint8_t>(SdStatus::FILE_MISSING);
}

inline bool packetTypeValid(uint8_t raw) {
    return raw == static_cast<uint8_t>(PacketType::COMMAND) ||
           raw == static_cast<uint8_t>(PacketType::STATUS);
}

inline bool volumeInRange(int16_t value) {
    return value >= kVolumeMin && value <= kVolumeMax;
}

inline uint16_t nextSequence(uint16_t current) {
    uint16_t n = static_cast<uint16_t>(current + 1u);
    if (n == 0) {
        n = 1;
    }
    return n;
}

// True when `incoming` is ahead of `last` inside a half-range window.
// Equal sequences are replays and are not newer.
inline bool sequenceIsNewer(uint16_t incoming, uint16_t last) {
    const uint16_t delta = static_cast<uint16_t>(incoming - last);
    return delta != 0 && delta < 0x8000u;
}

inline bool commandFieldsValid(const CommandPacket& p) {
    if (p.version != kProtocolVersion || p.epoch == 0 || p.sequence == 0) {
        return false;
    }
    if (!soundIdValid(p.sound)) {
        return false;
    }

    switch (p.command) {
        case CommandType::PLAY:
            return p.sound != SoundID::NONE &&
                   (p.param == CommandParam::PRESS || p.param == CommandParam::RELEASE) &&
                   p.value == 0;
        case CommandType::STOP:
            return p.sound == SoundID::NONE &&
                   (p.param == CommandParam::NONE || p.param == CommandParam::FAILSAFE) &&
                   p.value == 0;
        case CommandType::SET_VOLUME:
            return p.sound == SoundID::NONE &&
                   (p.param == CommandParam::GAIN_MASTER ||
                    p.param == CommandParam::GAIN_SIREN ||
                    p.param == CommandParam::GAIN_RUMBLER) &&
                   volumeInRange(p.value);
        case CommandType::RUMBLER_ENABLE:
        case CommandType::RUMBLER_DISABLE:
        case CommandType::HEARTBEAT:
        case CommandType::REQUEST_STATUS:
            return p.sound == SoundID::NONE && p.param == CommandParam::NONE && p.value == 0;
        case CommandType::NONE:
            return false;
    }
    return false;
}

inline bool statusFieldsValid(const StatusPacket& s) {
    if (s.version != kProtocolVersion || s.epoch == 0 || s.status_sequence == 0) {
        return false;
    }
    if (!soundIdValid(s.sound) || !teensyStatusValid(static_cast<uint8_t>(s.teensy)) ||
        !sdStatusValid(static_cast<uint8_t>(s.sd))) {
        return false;
    }
    if (s.volume_master > kVolumeMax || s.volume_siren > kVolumeMax ||
        s.volume_rumbler > kVolumeMax) {
        return false;
    }
    if (s.playing && s.sound == SoundID::NONE) {
        return false;
    }
    return true;
}

inline CommandPacket makeHeartbeat(uint8_t epoch, uint16_t sequence) {
    CommandPacket p = {};
    p.version = kProtocolVersion;
    p.epoch = epoch;
    p.command = CommandType::HEARTBEAT;
    p.sound = SoundID::NONE;
    p.param = CommandParam::NONE;
    p.value = 0;
    p.sequence = sequence;
    return p;
}

inline CommandPacket makePlay(uint8_t epoch, uint16_t sequence, SoundID sound, CommandParam edge) {
    CommandPacket p = {};
    p.version = kProtocolVersion;
    p.epoch = epoch;
    p.command = CommandType::PLAY;
    p.sound = sound;
    p.param = edge;
    p.value = 0;
    p.sequence = sequence;
    return p;
}

inline CommandPacket makeStop(uint8_t epoch, uint16_t sequence) {
    CommandPacket p = {};
    p.version = kProtocolVersion;
    p.epoch = epoch;
    p.command = CommandType::STOP;
    p.sound = SoundID::NONE;
    p.param = CommandParam::NONE;
    p.value = 0;
    p.sequence = sequence;
    return p;
}

inline CommandPacket makeFailsafeStop(uint8_t epoch, uint16_t sequence) {
    CommandPacket p = makeStop(epoch, sequence);
    p.param = CommandParam::FAILSAFE;
    return p;
}

inline CommandPacket makeRequestStatus(uint8_t epoch, uint16_t sequence) {
    CommandPacket p = makeHeartbeat(epoch, sequence);
    p.command = CommandType::REQUEST_STATUS;
    return p;
}

inline CommandPacket makeRumbler(uint8_t epoch, uint16_t sequence, bool enabled) {
    CommandPacket p = makeHeartbeat(epoch, sequence);
    p.command = enabled ? CommandType::RUMBLER_ENABLE : CommandType::RUMBLER_DISABLE;
    return p;
}

inline const char* commandName(CommandType command) {
    switch (command) {
        case CommandType::NONE:
            return "NONE";
        case CommandType::PLAY:
            return "PLAY";
        case CommandType::STOP:
            return "STOP";
        case CommandType::SET_VOLUME:
            return "VOLUME";
        case CommandType::RUMBLER_ENABLE:
            return "RUMBLER_ON";
        case CommandType::RUMBLER_DISABLE:
            return "RUMBLER_OFF";
        case CommandType::HEARTBEAT:
            return "HEARTBEAT";
        case CommandType::REQUEST_STATUS:
            return "STATUS";
    }
    return "?";
}

inline const char* paramName(CommandParam param) {
    switch (param) {
        case CommandParam::NONE:
            return "none";
        case CommandParam::PRESS:
            return "press";
        case CommandParam::RELEASE:
            return "release";
        case CommandParam::GAIN_MASTER:
            return "master";
        case CommandParam::GAIN_SIREN:
            return "siren";
        case CommandParam::GAIN_RUMBLER:
            return "rumbler";
        case CommandParam::FAILSAFE:
            return "failsafe";
    }
    return "?";
}

inline const char* admitName(AdmitResult result) {
    switch (result) {
        case AdmitResult::ACCEPT:
            return "ACCEPT";
        case AdmitResult::REJECT_MALFORMED:
            return "MALFORMED";
        case AdmitResult::REJECT_STALE:
            return "STALE";
        case AdmitResult::REJECT_EPOCH:
            return "EPOCH";
        case AdmitResult::REJECT_NEEDS_HEARTBEAT:
            return "NEEDS_HEARTBEAT";
    }
    return "?";
}

inline CommandPacket makeSetVolume(uint8_t epoch, uint16_t sequence, CommandParam gain, int16_t value) {
    CommandPacket p = {};
    p.version = kProtocolVersion;
    p.epoch = epoch;
    p.command = CommandType::SET_VOLUME;
    p.sound = SoundID::NONE;
    p.param = gain;
    p.value = value;
    p.sequence = sequence;
    return p;
}

// Playback-affecting radio/UART commands are ignored until a heartbeat has
// locked the sender's epoch. A changed epoch is locked only by a new
// heartbeat, so packets left in flight cannot start audio after a reboot
// or a link loss. STOP does not get a bypass: silence is already the default,
// and a fresh STOP still passes once its sequence is newer.
inline AdmitResult admitCommand(LinkGuard& link, const CommandPacket& cmd) {
    if (!commandFieldsValid(cmd)) {
        return AdmitResult::REJECT_MALFORMED;
    }

    const bool heartbeat = cmd.command == CommandType::HEARTBEAT;

    if (link.epoch == 0 || cmd.epoch != link.epoch) {
        if (!heartbeat) {
            return link.epoch == 0 ? AdmitResult::REJECT_NEEDS_HEARTBEAT
                                   : AdmitResult::REJECT_EPOCH;
        }
        link.epoch = cmd.epoch;
        link.last_sequence = cmd.sequence;
        link.heartbeat_seen = true;
        return AdmitResult::ACCEPT;
    }

    if (!sequenceIsNewer(cmd.sequence, link.last_sequence)) {
        return AdmitResult::REJECT_STALE;
    }

    if (!link.heartbeat_seen && !heartbeat) {
        return AdmitResult::REJECT_NEEDS_HEARTBEAT;
    }

    link.last_sequence = cmd.sequence;
    if (heartbeat) {
        link.heartbeat_seen = true;
    }
    return AdmitResult::ACCEPT;
}

inline bool elapsedMs(uint32_t now_ms, uint32_t then_ms, uint32_t timeout_ms) {
    return static_cast<uint32_t>(now_ms - then_ms) >= timeout_ms;
}

}  // namespace siren
