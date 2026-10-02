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
            // Phase 4 starts the WAV here. The log is the phase 2/3 echo.
            SIREN_LOG("commands: PLAY %s %s seq=%u epoch=%u\n", siren::soundName(cmd.sound),
                      siren::paramName(cmd.param), cmd.sequence, cmd.epoch);
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

    // Defense in depth for a dead bridge. playing() stays false until WAV
    // playback exists, so this does not fire on a silent bench.
    if (audio_engine::playing() && last_rx_ms_ != 0 &&
        siren::elapsedMs(now_ms, last_rx_ms_, siren::kLinkTimeoutMs)) {
        audio_engine::stop();
        last_rx_ms_ = now_ms;
        SIREN_LOG("commands: uart timeout, STOP\n");
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
    out.remote_epoch = 0;
    out.ack_sequence = 0;
    out.status_sequence = status_sequence_;
    return siren::statusFieldsValid(out);
}

}  // namespace command_handler
