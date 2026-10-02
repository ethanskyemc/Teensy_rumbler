#include "command_handler.h"

#include "audio_engine.h"
#include "debug.h"
#include "pins.h"
#include "sd_audio.h"
#include "sounds.h"

namespace command_handler {
namespace {

uint8_t epoch_ = 1;
uint16_t status_sequence_ = 1;
uint32_t last_rx_ms_ = 0;
siren::LinkGuard link_ = {};
siren::FrameParser parser_;
bool have_snapshot_ = false;
siren::StatusPacket snapshot_ = {};

bool audioChanged(const siren::StatusPacket& now) {
    if (!have_snapshot_) {
        return false;
    }
    return now.sound != snapshot_.sound || now.playing != snapshot_.playing ||
           now.rumbler_enabled != snapshot_.rumbler_enabled ||
           now.volume_master != snapshot_.volume_master || now.volume_siren != snapshot_.volume_siren ||
           now.volume_rumbler != snapshot_.volume_rumbler || now.teensy != snapshot_.teensy ||
           now.sd != snapshot_.sd || now.flags != snapshot_.flags;
}

bool publishStatus(bool quiet) {
    siren::StatusPacket status = {};
    if (!fillStatus(status)) {
        return false;
    }
    uint8_t frame[siren::kMaxFrameBytes];
    const size_t n = siren::buildStatusFrame(status, frame, sizeof(frame));
    if (n == 0 || Serial1.write(frame, n) != n) {
        if (!quiet) {
            SIREN_LOG("commands: status write failed\n");
        }
        return false;
    }
    snapshot_ = status;
    have_snapshot_ = true;
    status_sequence_ = siren::nextSequence(status_sequence_);
    if (quiet) {
        SIREN_LOG_VERBOSE("commands: status seq=%u\n", status.status_sequence);
        return true;
    }
    SIREN_LOG("commands: status %s playing=%u rumbler=%u vol=%u ack=%u\n",
              siren::soundName(status.sound), status.playing ? 1u : 0u,
              status.rumbler_enabled ? 1u : 0u, status.volume_master, status.ack_sequence);
    return true;
}

void applyCommand(const siren::CommandPacket& cmd) {
    switch (cmd.command) {
        case siren::CommandType::HEARTBEAT:
            SIREN_LOG_VERBOSE("commands: heartbeat seq=%u epoch=%u\n", cmd.sequence, cmd.epoch);
            break;
        case siren::CommandType::STOP:
            audio_engine::stop();
            SIREN_LOG("commands: STOP %s seq=%u epoch=%u\n",
                      cmd.param == siren::CommandParam::FAILSAFE ? "failsafe" : "all", cmd.sequence,
                      cmd.epoch);
            break;
        case siren::CommandType::PLAY:
            SIREN_LOG("commands: PLAY %s %s seq=%u epoch=%u\n", siren::soundName(cmd.sound),
                      siren::paramName(cmd.param), cmd.sequence, cmd.epoch);
            audio_engine::requestPlay(cmd.sound, cmd.param);
            break;
        case siren::CommandType::SET_VOLUME: {
            const uint8_t level = static_cast<uint8_t>(cmd.value);
            if (cmd.param == siren::CommandParam::GAIN_MASTER) {
                audio_engine::setMasterVolume(level);
            } else if (cmd.param == siren::CommandParam::GAIN_SIREN) {
                audio_engine::setSirenGain(level);
            } else if (cmd.param == siren::CommandParam::GAIN_RUMBLER) {
                audio_engine::setRumblerGain(level);
            }
            SIREN_LOG("commands: VOLUME %s %u seq=%u epoch=%u\n", siren::paramName(cmd.param), level,
                      cmd.sequence, cmd.epoch);
            break;
        }
        case siren::CommandType::RUMBLER_ENABLE:
            audio_engine::setRumblerEnabled(true);
            SIREN_LOG("commands: RUMBLER_ON seq=%u epoch=%u\n", cmd.sequence, cmd.epoch);
            break;
        case siren::CommandType::RUMBLER_DISABLE:
            audio_engine::setRumblerEnabled(false);
            SIREN_LOG("commands: RUMBLER_OFF seq=%u epoch=%u\n", cmd.sequence, cmd.epoch);
            break;
        case siren::CommandType::REQUEST_STATUS:
            SIREN_LOG("commands: STATUS request seq=%u epoch=%u\n", cmd.sequence, cmd.epoch);
            break;
        case siren::CommandType::NONE:
            break;
    }
}

void handleFrame(uint32_t now_ms) {
    if (parser_.type() != siren::PacketType::COMMAND) {
        SIREN_LOG("commands: ignored type %u\n", static_cast<unsigned>(parser_.type()));
        return;
    }
    siren::CommandPacket cmd = {};
    if (!siren::deserializeCommand(parser_.payload(), parser_.payloadLength(), cmd)) {
        SIREN_LOG("commands: bad command payload\n");
        return;
    }
    const siren::AdmitResult result = siren::admitCommand(link_, cmd);
    if (result != siren::AdmitResult::ACCEPT) {
        if (cmd.command != siren::CommandType::HEARTBEAT) {
            SIREN_LOG("commands: reject %s %s seq=%u epoch=%u\n", siren::admitName(result),
                      siren::commandName(cmd.command), cmd.sequence, cmd.epoch);
        }
        return;
    }
    last_rx_ms_ = now_ms;
    applyCommand(cmd);
    publishStatus(cmd.command == siren::CommandType::HEARTBEAT);
}

}  // namespace

void begin() {
    uint8_t epoch = static_cast<uint8_t>(micros() & 0xFFu);
    if (epoch == 0) {
        epoch = 1;
    }
    epoch_ = epoch;
    status_sequence_ = 1;
    last_rx_ms_ = 0;
    link_ = {};
    parser_.reset();
    have_snapshot_ = false;
    snapshot_ = {};

    // Pins 0 and 1 are Teensy 4.0 Serial1. Set them explicitly so the UART
    // cannot move to an alternate pad.
    Serial1.setRX(pins::kSerial1Rx);
    Serial1.setTX(pins::kSerial1Tx);
    Serial1.begin(siren::kUartBaud);
    SIREN_LOG("commands: Serial1 %u baud rx=%u tx=%u\n", siren::kUartBaud, pins::kSerial1Rx,
              pins::kSerial1Tx);

    siren::StatusPacket status = {};
    if (!fillStatus(status) || !siren::statusFieldsValid(status)) {
        SIREN_LOG("commands: status template invalid\n");
        return;
    }
    SIREN_LOG("commands: silent boot epoch=%u master=%u\n", status.epoch, status.volume_master);
}

void poll(uint32_t now_ms) {
    while (Serial1.available() > 0) {
        const int value = Serial1.read();
        if (value < 0) {
            break;
        }
        if (!parser_.push(static_cast<uint8_t>(value), now_ms)) {
            continue;
        }
        handleFrame(now_ms);
    }

    // A dead bridge must not leave a siren running. Heartbeats refresh
    // last_rx_ms_; this fires only while a WAV is actually playing.
    if (audio_engine::playing() && last_rx_ms_ != 0 &&
        siren::elapsedMs(now_ms, last_rx_ms_, siren::kLinkTimeoutMs)) {
        audio_engine::stop();
        last_rx_ms_ = now_ms;
        SIREN_LOG("commands: uart timeout, STOP\n");
    }

    siren::StatusPacket now = {};
    if (fillStatus(now) && audioChanged(now)) {
        publishStatus(false);
    }
}

bool fillStatus(siren::StatusPacket& out) {
    out = {};
    out.version = siren::kProtocolVersion;
    out.epoch = epoch_;
    out.sound = audio_engine::currentSound();
    out.playing = audio_engine::playing();
    out.rumbler_enabled = audio_engine::rumblerEnabled();
    out.volume_master = audio_engine::masterVolume();
    out.volume_siren = audio_engine::sirenGain();
    out.volume_rumbler = audio_engine::rumblerGain();
    out.teensy = audio_engine::deviceStatus();
    out.sd = sd_audio::status();
    out.flags = 0;
    if (out.teensy == siren::TeensyStatus::READY) {
        out.flags = static_cast<uint8_t>(out.flags | siren::STATUS_FLAG_AUDIO_READY);
    }
    out.remote_epoch = link_.heartbeat_seen ? link_.epoch : 0;
    out.ack_sequence = link_.heartbeat_seen ? link_.last_sequence : 0;
    out.status_sequence = status_sequence_;
    return siren::statusFieldsValid(out);
}

}  // namespace command_handler
