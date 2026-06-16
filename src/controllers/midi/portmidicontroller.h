#pragma once

#include <portmidi.h>

#include <QByteArray>
#include <QScopedPointer>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

#include "controllers/midi/midicontroller.h"
#include "controllers/midi/portmididevice.h"

// Note:
// A standard Midi device runs at 31.25 kbps, with 10 bits / byte
// 1 byte / 320 microseconds
// a usual Midi message has 3 byte which results to
// 1042.6 messages per second
//
// The MIDI over IEEE-1394:
// http://www.midi.org/techspecs/rp27v10spec%281394%29.pdf
// which is also used for USB defines 3 speeds:
// 1 byte / 320 microseconds
// 2 bytes / 320 microseconds
// 3 bytes / 320 microseconds
// which results in up to 3125 messages per second
// if we assume normal 3 Byte messages.
//
// For instants the SCS.1d, uses the 3 x speed
//
// Due to a bug Mixxx completely stops responding to the controller
// if more than this number of messages queue up. Don't lower this (much.)
// The SCS.1d a 3x Speed device
// accumulated 500 messages in a single poll during stress-testing.
// A midi message contains 1 .. 4 bytes.
// a 1024 messages buffer will buffer ~327 ms Midi-Stream
#define MIXXX_PORTMIDI_BUFFER_LEN 1024

// Length of SysEx buffer in byte
#define MIXXX_SYSEX_BUFFER_LEN 1024

// String to display for no MIDI devices present
#define MIXXX_PORTMIDI_NO_DEVICE_STRING "None"

/// PortMidi-based implementation of MidiController
///
/// This class is represents a MIDI device, either physical or software.
/// It uses the PortMidi API to send and receive MIDI messages to/from the device.
/// It's important to note that PortMidi treats input and output on a single
/// physical device as two separate half-duplex devices. In this class, we wrap
/// those together into a single device, which is why the constructor takes
/// both arguments pertaining to both input and output "devices".
class PortMidiController : public MidiController {
    Q_OBJECT
  public:
    PortMidiController(const PmDeviceInfo* inputDeviceInfo,
            const PmDeviceInfo* outputDeviceInfo,
            int inputDeviceIndex,
            int outputDeviceIndex);
    ~PortMidiController() override;

    PhysicalTransportProtocol getPhysicalTransportProtocol() const override {
        return PhysicalTransportProtocol::UNKNOWN;
    }

    QString getVendorString() const override {
        return QString();
    }
    QString getProductString() const override {
        if (m_pInputDevice) {
            return QString::fromLocal8Bit(m_pInputDevice->info()->name);
        }
        if (m_pOutputDevice) {
            return QString::fromLocal8Bit(m_pOutputDevice->info()->name);
        }
        return QString();
    }
    std::optional<uint16_t> getVendorId() const override {
        return std::nullopt;
    }
    std::optional<uint16_t> getProductId() const override {
        return std::nullopt;
    }
    QString getSerialNumber() const override {
        return QString();
    }

    std::optional<uint8_t> getUsbInterfaceNumber() const override {
        return std::nullopt;
    }

  private slots:
    bool poll() override;

  protected:
    // MockPortMidiController needs this to not be private.
    void sendShortMsg(unsigned char status, unsigned char byte1,
                      unsigned char byte2) override;

  private:
    int open(const QString& resourcePath) override;
    int close() override;

    // The sysex data must already contain the start byte 0xf0 and the end byte
    // 0xf7.
    bool sendBytes(const QByteArray& data) override;

    bool isPolling() const override {
        return true;
    }

    // For testing only so that test fixtures can install mock PortMidiDevices.
    void setPortMidiInputDevice(PortMidiDevice* device) {
        m_pInputDevice.reset(device);
    }
    void setPortMidiOutputDevice(PortMidiDevice* device) {
        m_pOutputDevice.reset(device);
    }

    // For testing only: keep output synchronous so test fixtures can verify the
    // exact writeShort()/writeSysEx() calls right after a send.
    void setOutputThreadEnabledForTesting(bool enabled) {
        m_outputThreadEnabled = enabled;
    }

    // A single queued outgoing MIDI message. Short messages carry a packed
    // PortMidi word; sysex messages carry the raw bytes (including 0xF0..0xF7).
    struct OutputMessage {
        bool isSysex;
        int32_t shortWord;
        QByteArray sysex;
    };

    // Output worker: a dedicated thread drains m_outputQueue and performs the
    // actual blocking PortMidi writes. This keeps a slow or erroring sysex write
    // from stalling the input-polling thread (PortMidi's Windows backend makes
    // Pm_WriteSysEx block and can return pmHostError under load).
    void startOutputThread();
    void stopOutputThread();
    void outputWorker();
    void writeOutputNow(const OutputMessage& message);

    QScopedPointer<PortMidiDevice> m_pInputDevice;
    QScopedPointer<PortMidiDevice> m_pOutputDevice;

    PmEvent m_midiBuffer[MIXXX_PORTMIDI_BUFFER_LEN];

    // Storage for SysEx messages
    unsigned char m_cReceiveMsg[MIXXX_SYSEX_BUFFER_LEN];
    int m_cReceiveMsg_index;
    bool m_bInSysex;

    // Output queue + worker thread. m_outputThreadEnabled/m_outputThreadRunning
    // are only touched on the controller thread (open/close/send); the queue and
    // the two flags below are guarded by m_outputMutex.
    std::thread m_outputThread;
    std::mutex m_outputMutex;
    std::condition_variable m_outputCond;
    std::deque<OutputMessage> m_outputQueue;
    bool m_outputThreadStop;
    bool m_outputThreadRunning;
    bool m_outputThreadEnabled;

    friend class PortMidiControllerTest;
};
