#include "controllers/midi/portmidicontroller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <QByteArray>
#include <QScopedPointer>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "controllers/midi/portmididevice.h"
#include "test/mixxxtest.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::Sequence;
using ::testing::SetArrayArgument;

namespace {
// Thread-safe sink that the mock device writes into from the output worker
// thread, so the test thread can wait for and then inspect what was sent.
class OutputRecorder {
  public:
    void recordShort(int32_t word) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shortWords.push_back(word);
        ++m_total;
        m_cond.notify_all();
    }
    void recordSysex(const QByteArray& data) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sysex.push_back(data);
        ++m_total;
        m_cond.notify_all();
    }
    bool waitForTotal(int expected, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cond.wait_for(lock, timeout, [this, expected] {
            return m_total >= expected;
        });
    }
    int total() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_total;
    }
    std::vector<int32_t> shortWords() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_shortWords;
    }
    std::vector<QByteArray> sysexMessages() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_sysex;
    }

  private:
    std::mutex m_mutex;
    std::condition_variable m_cond;
    int m_total = 0;
    std::vector<int32_t> m_shortWords;
    std::vector<QByteArray> m_sysex;
};

// PortMidi hands writeSysEx a bare pointer terminated by 0xF7; copy it out for
// comparison.
QByteArray sysexToByteArray(unsigned char* message) {
    QByteArray out;
    for (int i = 0; i < MIXXX_SYSEX_BUFFER_LEN; ++i) {
        out.append(static_cast<char>(message[i]));
        if (message[i] == 0xF7) {
            break;
        }
    }
    return out;
}
} // namespace

class MockPortMidiController : public PortMidiController {
  public:
    MockPortMidiController(const PmDeviceInfo* inputDeviceInfo,
            const PmDeviceInfo* outputDeviceInfo,
            int inputDeviceIndex,
            int outputDeviceIndex)
            : PortMidiController(inputDeviceInfo,
                      outputDeviceInfo,
                      inputDeviceIndex,
                      outputDeviceIndex) {
    }
    ~MockPortMidiController() override {
    }

    void sendShortMsg(unsigned char status, unsigned char byte1, unsigned char byte2) override {
        PortMidiController::sendShortMsg(status, byte1, byte2);
    }

    void sendSysexMsg(const QList<int>& data, unsigned int length) {
        PortMidiController::sendSysexMsg(data, length);
    }

    MOCK_METHOD4(receivedShortMessage,
            void(unsigned char, unsigned char, unsigned char, mixxx::Duration));
    MOCK_METHOD2(receive, void(const QByteArray&, mixxx::Duration));

    // These tests are unrelated to scripting.
    MOCK_METHOD0(startEngine, void());
    MOCK_METHOD0(stopEngine, void());
};

class MockPortMidiDevice : public PortMidiDevice {
  public:
    MockPortMidiDevice(PmDeviceInfo* info, int index)
            : PortMidiDevice(info, index) {
    }

    MOCK_CONST_METHOD0(isOpen, bool());
    MOCK_METHOD1(openInput, PmError(int32_t));
    MOCK_METHOD0(openOutput, PmError());
    MOCK_METHOD0(close, PmError());
    MOCK_METHOD0(poll, PmError());
    MOCK_METHOD2(read, int(PmEvent*, int32_t));
    MOCK_METHOD1(writeShort, PmError(int32_t));
    MOCK_METHOD1(writeSysEx, PmError(unsigned char*));
};

class PortMidiControllerTest : public MixxxTest {
  protected:
    PortMidiControllerTest()
            : m_mockInput(new MockPortMidiDevice(&m_inputDeviceInfo, 0)),
              m_mockOutput(new MockPortMidiDevice(&m_outputDeviceInfo, 0)) {
        // PmDeviceInfo::name is non const since portmidi 2.0.1
        // We maintain the memory here in place of Pm_GetDeviceInfo()
        char inputDeviceName[] = "Test Input Device";
        char outputDeviceName[] = "Test Output Device";
        constexpr const char interf[] = "Test";
        m_inputDeviceInfo.name = inputDeviceName;
        m_inputDeviceInfo.interf = interf;
        m_inputDeviceInfo.input = 1;
        m_inputDeviceInfo.output = 0;
        m_inputDeviceInfo.opened = 0;

        m_outputDeviceInfo.name = outputDeviceName;
        m_outputDeviceInfo.interf = interf;
        m_outputDeviceInfo.input = 0;
        m_outputDeviceInfo.output = 1;
        m_outputDeviceInfo.opened = 0;

        m_pController.reset(new MockPortMidiController(
                &m_inputDeviceInfo, &m_outputDeviceInfo, 0, 0));
        m_pController->setPortMidiInputDevice(m_mockInput);
        m_pController->setPortMidiOutputDevice(m_mockOutput);
        // Keep output synchronous so the writeShort()/writeSysEx() expectations
        // below fire on the calling thread rather than a background worker.
        m_pController->setOutputThreadEnabledForTesting(false);
    }

    void TearDown() override {
        // Never let an output worker thread outlive the controller / mock device.
        if (m_pController) {
            m_pController->stopOutputThread();
        }
        MixxxTest::TearDown();
    }

    void openDevice() {
        m_pController->open({});
    }

    void closeDevice() {
        m_pController->close();
    }

    void pollDevice() {
        m_pController->poll();
    }

    // Output-thread control for the asynchronous-send tests below. The fixture
    // is a friend of PortMidiController, so it can drive the worker directly
    // without going through the full open()/close() lifecycle.
    void enableOutputThread() {
        m_pController->setOutputThreadEnabledForTesting(true);
    }

    void startOutputThread() {
        m_pController->startOutputThread();
    }

    void stopOutputThread() {
        m_pController->stopOutputThread();
    }

    PmDeviceInfo m_inputDeviceInfo;
    PmDeviceInfo m_outputDeviceInfo;
    MockPortMidiDevice* m_mockInput;
    MockPortMidiDevice* m_mockOutput;
    QScopedPointer<MockPortMidiController> m_pController;
};

PmEvent MakeEvent(PmMessage message, PmTimestamp timestamp) {
    PmEvent event;
    event.message = message;
    event.timestamp = timestamp;
    return event;
}

MATCHER_P(ByteArrayEquals, value,
          "Checks that the non-NULL terminated argument array exactly equals "
          "the provided byte container.") {
    for (int i = 0; i < value.size(); ++i) {
        if (arg[i] != value.at(i))
            return false;
    }
    return true;
}

TEST_F(PortMidiControllerTest, OpenClose) {
    Sequence input;
    ON_CALL(*m_mockInput, isOpen())
            .WillByDefault(Return(false));
    EXPECT_CALL(*m_mockInput, openInput(MIXXX_PORTMIDI_BUFFER_LEN))
            .InSequence(input)
            .WillOnce(Return(pmNoError));
    EXPECT_CALL(*m_mockInput, isOpen())
            .InSequence(input)
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, close())
            .InSequence(input)
            .WillOnce(Return(pmNoError));

    Sequence output;
    ON_CALL(*m_mockOutput, isOpen())
            .WillByDefault(Return(false));
    EXPECT_CALL(*m_mockOutput, openOutput())
            .WillOnce(Return(pmNoError));
    EXPECT_CALL(*m_mockOutput, isOpen())
            .InSequence(output)
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, close())
            .InSequence(output)
            .WillOnce(Return(pmNoError));

    openDevice();
    EXPECT_TRUE(m_pController->isOpen());
    closeDevice();
    EXPECT_FALSE(m_pController->isOpen());
};

TEST_F(PortMidiControllerTest, WriteShort) {
    // Note that Pm_WriteShort takes an int32_t formatted as 0x00B2B1SS where SS
    // is the status byte, B1 is the first message byte and B2 is the second
    // message byte.
    Sequence output;
    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeShort(0x403C90))
            .InSequence(output)
            .WillOnce(Return(pmNoError));
    EXPECT_CALL(*m_mockOutput, writeShort(0xFFFFFF))
            .InSequence(output)
            .WillOnce(Return(pmBadData));
    EXPECT_CALL(*m_mockOutput, writeShort(0x403C80))
            .InSequence(output)
            .WillOnce(Return(pmNoError));

    m_pController->sendShortMsg(0x90, 0x3C, 0x40);
    m_pController->sendShortMsg(0xFF, 0xFF, 0xFF);
    m_pController->sendShortMsg(0x80, 0x3C, 0x40);
};

TEST_F(PortMidiControllerTest, WriteSysex) {
    QList<int> sysex;
    sysex.append(0xF0);
    sysex.append(0x12);
    sysex.append(0xF7);

    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeSysEx(ByteArrayEquals(sysex)))
            .WillOnce(Return(pmNoError));
    m_pController->sendSysexMsg(sysex, sysex.length());
};

TEST_F(PortMidiControllerTest, WriteSysex_Malformed) {
    QList<int> sysex;
    sysex.append(0xF0);
    sysex.append(0x12);

    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeSysEx(_))
            .Times(0);
    m_pController->sendSysexMsg(sysex, sysex.length());
};


TEST_F(PortMidiControllerTest, Poll_Read_Basic) {
    std::vector<PmEvent> messages;
    messages.push_back(MakeEvent(0x403C90, 0x0));
    messages.push_back(MakeEvent(0x403C80, 0x1));

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages.begin(), messages.end()),
                    Return(static_cast<int>(messages.size()))));

    EXPECT_CALL(*m_pController, receivedShortMessage(0x90, 0x3C, 0x40, _))
            .InSequence(read);
    EXPECT_CALL(*m_pController, receivedShortMessage(0x80, 0x3C, 0x40, _))
            .InSequence(read);

    pollDevice();
};

TEST_F(PortMidiControllerTest, Poll_Read_SysExWithRealtime) {
    std::vector<PmEvent> messages;
    messages.push_back(MakeEvent(0x332211F0, 0x0));
    messages.push_back(MakeEvent(0x000000F8, 0x1));
    messages.push_back(MakeEvent(0x77665544, 0x0));
    messages.push_back(MakeEvent(0x000000FA, 0x2));
    messages.push_back(MakeEvent(0x000000F7, 0x0));

    QByteArray sysex;
    sysex.append('\xF0');
    sysex.append('\x11');
    sysex.append('\x22');
    sysex.append('\x33');
    sysex.append('\x44');
    sysex.append('\x55');
    sysex.append('\x66');
    sysex.append('\x77');
    sysex.append('\xF7');

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages.begin(), messages.end()),
                            Return(messages.size())));
    EXPECT_CALL(*m_pController, receivedShortMessage(0xF8, 0x00, 0x00, _))
            .InSequence(read);
    EXPECT_CALL(*m_pController, receivedShortMessage(0xFA, 0x00, 0x00, _))
            .InSequence(read);
    EXPECT_CALL(*m_pController, receive(sysex, _))
            .InSequence(read);

    pollDevice();
};

TEST_F(PortMidiControllerTest, Poll_Read_SysEx) {
    std::vector<PmEvent> messages;
    messages.push_back(MakeEvent(0x332211F0, 0x0));
    messages.push_back(MakeEvent(0xF7665544, 0x1));

    QByteArray sysex;
    sysex.append('\xF0');
    sysex.append('\x11');
    sysex.append('\x22');
    sysex.append('\x33');
    sysex.append('\x44');
    sysex.append('\x55');
    sysex.append('\x66');
    sysex.append('\xF7');

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages.begin(), messages.end()),
                            Return(messages.size())));
    EXPECT_CALL(*m_pController, receive(sysex, _))
            .InSequence(read);

    pollDevice();
};

TEST_F(PortMidiControllerTest,
       Poll_Read_SysExWithRealtime_CoincidentalRealtimeByte) {
    // We used to incorrectly treat an 0xF8 occurring in a SysEx message as a
    // realtime message. This test verifies that we do not do this anymore.
    std::vector<PmEvent> messages;
    messages.push_back(MakeEvent(0x332211F0, 0x0));
    messages.push_back(MakeEvent(0x6655F844, 0x0));
    messages.push_back(MakeEvent(0x0000F777, 0x0));

    QByteArray sysex;
    sysex.append('\xF0');
    sysex.append('\x11');
    sysex.append('\x22');
    sysex.append('\x33');
    sysex.append('\x44');
    sysex.append('\xF8');
    sysex.append('\x55');
    sysex.append('\x66');
    sysex.append('\x77');
    sysex.append('\xF7');

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages.begin(), messages.end()),
                            Return(messages.size())));
    EXPECT_CALL(*m_pController, receive(sysex, _))
            .InSequence(read);

    pollDevice();
};

TEST_F(PortMidiControllerTest, Poll_Read_SysExInterrupted_FollowedByNormalMessage) {
    // According to the PortMIDI documentation when a SysEx message is
    // interrupted, we will expect to see a non-realtime status byte as a new
    // message before seeing an EOX terminating SysEx. In this event we drop the
    // SysEx message and process the new message as normal.

    std::vector<PmEvent> messages;
    messages.push_back(MakeEvent(0x332211F0, 0x0));
    messages.push_back(MakeEvent(0x00403C90, 0x0));

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages.begin(), messages.end()),
                            Return(messages.size())));
    EXPECT_CALL(*m_pController, receivedShortMessage(0x90, 0x3C, 0x40, _))
            .InSequence(read);

    pollDevice();
};

TEST_F(PortMidiControllerTest, Poll_Read_SysExInterrupted_FollowedBySysExMessage) {
    // According to the PortMIDI documentation when a SysEx message is
    // interrupted, we will expect to see a non-realtime status byte as a new
    // message before seeing an EOX terminating SysEx. In this event we drop the
    // SysEx message and process the new message as normal.

    std::vector<PmEvent> messages;
    messages.push_back(MakeEvent(0x332211F0, 0x0));
    messages.push_back(MakeEvent(0x77665544, 0x0));
    messages.push_back(MakeEvent(0x332211F0, 0x1));
    messages.push_back(MakeEvent(0xF7665544, 0x0));

    QByteArray sysex;
    sysex.append('\xF0');
    sysex.append('\x11');
    sysex.append('\x22');
    sysex.append('\x33');
    sysex.append('\x44');
    sysex.append('\x55');
    sysex.append('\x66');
    sysex.append('\xF7');

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages.begin(), messages.end()),
                            Return(messages.size())));
    EXPECT_CALL(*m_pController, receive(sysex, _))
            .InSequence(read);

    pollDevice();
};


TEST_F(PortMidiControllerTest, Poll_Read_SysEx_BufferOverflow) {
    // According to the PortMIDI documentation when a SysEx message is
    // interrupted, we will expect to see a non-realtime status byte as a new
    // message before seeing an EOX terminating SysEx. In this event we drop the
    // SysEx message and process the new message as normal.

    std::vector<PmEvent> messages1;
    messages1.push_back(MakeEvent(0x332211F0, 0x0));
    messages1.push_back(MakeEvent(0x77665544, 0x0));

    std::vector<PmEvent> messages2;
    messages2.push_back(MakeEvent(0x332211F0, 0x1));

    std::vector<PmEvent> messages3;
    messages3.push_back(MakeEvent(0xF7665544, 0x2));

    QByteArray sysex;
    sysex.append('\xF0');
    sysex.append('\x11');
    sysex.append('\x22');
    sysex.append('\x33');
    sysex.append('\x44');
    sysex.append('\x55');
    sysex.append('\x66');
    sysex.append('\xF7');

    Sequence read;
    EXPECT_CALL(*m_mockInput, isOpen())
            .WillRepeatedly(Return(true));

    // Poll 1 -- returns messages1.
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages1.begin(), messages1.end()),
                            Return(messages1.size())));

    // Poll 2 -- buffer overflow.
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(Return(pmBufferOverflow));

    // Poll 3 -- returns messages2.
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages2.begin(), messages2.end()),
                            Return(messages2.size())));

    // Poll 4 -- returns messages3.
    EXPECT_CALL(*m_mockInput, read(NotNull(), _))
            .InSequence(read)
            .WillOnce(DoAll(SetArrayArgument<0>(messages3.begin(), messages3.end()),
                            Return(messages3.size())));
    EXPECT_CALL(*m_pController, receive(sysex, _))
            .InSequence(read);

    pollDevice();
    pollDevice();
    pollDevice();
    pollDevice();
};

// ===========================================================================
// Asynchronous output queue / worker thread tests.
// ===========================================================================

TEST_F(PortMidiControllerTest, AsyncOutputDeliversShortMessage) {
    OutputRecorder recorder;
    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeShort(0x403C90))
            .WillOnce(Invoke([&](int32_t word) {
                recorder.recordShort(word);
                return pmNoError;
            }));

    enableOutputThread();
    startOutputThread();
    m_pController->sendShortMsg(0x90, 0x3C, 0x40);

    // Join the worker before asserting so a timeout failure can never leave it
    // running with dangling references to this stack frame.
    const bool delivered = recorder.waitForTotal(1, std::chrono::seconds(2));
    stopOutputThread();
    ASSERT_TRUE(delivered);

    ASSERT_EQ(1u, recorder.shortWords().size());
    EXPECT_EQ(0x403C90, recorder.shortWords()[0]);
};

TEST_F(PortMidiControllerTest, AsyncOutputDeliversSysex) {
    QList<int> sysex;
    sysex.append(0xF0);
    sysex.append(0x12);
    sysex.append(0x34);
    sysex.append(0xF7);

    OutputRecorder recorder;
    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeSysEx(_))
            .WillOnce(Invoke([&](unsigned char* message) {
                recorder.recordSysex(sysexToByteArray(message));
                return pmNoError;
            }));

    enableOutputThread();
    startOutputThread();
    m_pController->sendSysexMsg(sysex, sysex.length());

    const bool delivered = recorder.waitForTotal(1, std::chrono::seconds(2));
    stopOutputThread();
    ASSERT_TRUE(delivered);

    ASSERT_EQ(1u, recorder.sysexMessages().size());
    QByteArray expected;
    expected.append(static_cast<char>(0xF0));
    expected.append(static_cast<char>(0x12));
    expected.append(static_cast<char>(0x34));
    expected.append(static_cast<char>(0xF7));
    EXPECT_EQ(expected, recorder.sysexMessages()[0]);
};

TEST_F(PortMidiControllerTest, AsyncOutputPreservesOrder) {
    OutputRecorder recorder;
    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeShort(_))
            .WillRepeatedly(Invoke([&](int32_t word) {
                recorder.recordShort(word);
                return pmNoError;
            }));

    enableOutputThread();
    startOutputThread();

    const int count = 50;
    for (int i = 0; i < count; ++i) {
        m_pController->sendShortMsg(0x90, static_cast<unsigned char>(i), 0x40);
    }

    const bool delivered = recorder.waitForTotal(count, std::chrono::seconds(5));
    stopOutputThread();
    ASSERT_TRUE(delivered);

    auto words = recorder.shortWords();
    ASSERT_EQ(static_cast<size_t>(count), words.size());
    for (int i = 0; i < count; ++i) {
        const int32_t expected = (0x40 << 16) | (i << 8) | 0x90;
        EXPECT_EQ(expected, words[i]) << "out of order at index " << i;
    }
};

TEST_F(PortMidiControllerTest, AsyncSysexHostErrorRetriedThenSucceeds) {
    QList<int> sysex;
    sysex.append(0xF0);
    sysex.append(0x55);
    sysex.append(0xF7);

    OutputRecorder recorder;
    std::atomic<int> attempts{0};
    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    // First two attempts report a host error, the third succeeds.
    EXPECT_CALL(*m_mockOutput, writeSysEx(_))
            .Times(3)
            .WillRepeatedly(Invoke([&](unsigned char* message) {
                const int n = ++attempts;
                recorder.recordSysex(sysexToByteArray(message));
                return n < 3 ? pmHostError : pmNoError;
            }));

    enableOutputThread();
    startOutputThread();
    m_pController->sendSysexMsg(sysex, sysex.length());

    const bool delivered = recorder.waitForTotal(3, std::chrono::seconds(2));
    stopOutputThread();
    ASSERT_TRUE(delivered);

    EXPECT_EQ(3, attempts.load());
};

TEST_F(PortMidiControllerTest, AsyncSysexHostErrorGivesUpAfterRetries) {
    QList<int> sysex;
    sysex.append(0xF0);
    sysex.append(0x66);
    sysex.append(0xF7);

    OutputRecorder recorder;
    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    // Always fail. The worker must give up after the retry limit (3) and must
    // NOT loop forever -- the Times(3) makes a 4th call fail the test.
    EXPECT_CALL(*m_mockOutput, writeSysEx(_))
            .Times(3)
            .WillRepeatedly(Invoke([&](unsigned char* message) {
                recorder.recordSysex(sysexToByteArray(message));
                return pmHostError;
            }));

    enableOutputThread();
    startOutputThread();
    m_pController->sendSysexMsg(sysex, sysex.length());

    const bool delivered = recorder.waitForTotal(3, std::chrono::seconds(2));
    // Give the worker time to (incorrectly) attempt a 4th send, if it would.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stopOutputThread();
    ASSERT_TRUE(delivered);

    EXPECT_EQ(3, recorder.total());
};

TEST_F(PortMidiControllerTest, AsyncShutdownDrainsQueuedMessages) {
    OutputRecorder recorder;

    std::mutex gateMutex;
    std::condition_variable gateCond;
    bool released = false;
    bool firstEntered = false;
    std::atomic<int> calls{0};

    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeSysEx(_))
            .Times(3)
            .WillRepeatedly(Invoke([&](unsigned char* message) {
                if (++calls == 1) {
                    // Block the worker inside the first send so the next two
                    // messages pile up in the queue before we request stop.
                    std::unique_lock<std::mutex> lock(gateMutex);
                    firstEntered = true;
                    gateCond.notify_all();
                    gateCond.wait(lock, [&] { return released; });
                }
                recorder.recordSysex(sysexToByteArray(message));
                return pmNoError;
            }));

    enableOutputThread();
    startOutputThread();

    QList<int> a;
    a.append(0xF0);
    a.append(0x01);
    a.append(0xF7);
    QList<int> b;
    b.append(0xF0);
    b.append(0x02);
    b.append(0xF7);
    QList<int> c;
    c.append(0xF0);
    c.append(0x03);
    c.append(0xF7);

    m_pController->sendSysexMsg(a, a.length());
    bool entered = false;
    {
        std::unique_lock<std::mutex> lock(gateMutex);
        entered = gateCond.wait_for(lock, std::chrono::seconds(2), [&] {
            return firstEntered;
        });
    }
    if (entered) {
        // Queue two more while the worker is parked in the first send.
        m_pController->sendSysexMsg(b, b.length());
        m_pController->sendSysexMsg(c, c.length());
    }

    // Always release the worker and join before asserting; the queued b and c
    // must still flush before the worker exits.
    {
        std::lock_guard<std::mutex> lock(gateMutex);
        released = true;
    }
    gateCond.notify_all();
    stopOutputThread();
    ASSERT_TRUE(entered);

    EXPECT_EQ(3, recorder.total());
    auto msgs = recorder.sysexMessages();
    ASSERT_EQ(3u, msgs.size());
    ASSERT_GE(msgs[0].size(), 2);
    ASSERT_GE(msgs[1].size(), 2);
    ASSERT_GE(msgs[2].size(), 2);
    EXPECT_EQ(static_cast<char>(0x01), msgs[0].at(1));
    EXPECT_EQ(static_cast<char>(0x02), msgs[1].at(1));
    EXPECT_EQ(static_cast<char>(0x03), msgs[2].at(1));
};

TEST_F(PortMidiControllerTest, AsyncMultipleStartStopCycles) {
    OutputRecorder recorder;
    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeShort(_))
            .WillRepeatedly(Invoke([&](int32_t word) {
                recorder.recordShort(word);
                return pmNoError;
            }));

    enableOutputThread();
    for (int cycle = 0; cycle < 3; ++cycle) {
        startOutputThread();
        m_pController->sendShortMsg(0x90, 0x10, 0x40);
        const bool delivered =
                recorder.waitForTotal(cycle + 1, std::chrono::seconds(2));
        stopOutputThread();
        ASSERT_TRUE(delivered);
    }

    EXPECT_EQ(3, recorder.total());
};

TEST_F(PortMidiControllerTest, AsyncSendIgnoredWhenOutputClosed) {
    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(false));
    // Nothing should be written when the output device reports closed.
    EXPECT_CALL(*m_mockOutput, writeShort(_))
            .Times(0);
    EXPECT_CALL(*m_mockOutput, writeSysEx(_))
            .Times(0);

    enableOutputThread();
    startOutputThread();
    m_pController->sendShortMsg(0x90, 0x3C, 0x40);
    QList<int> sysex;
    sysex.append(0xF0);
    sysex.append(0x12);
    sysex.append(0xF7);
    m_pController->sendSysexMsg(sysex, sysex.length());

    // Let the worker run; it must not call write* on a closed device.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stopOutputThread();
};

TEST_F(PortMidiControllerTest, AsyncQueueBoundDropsWhenFull) {
    OutputRecorder recorder;

    std::mutex gateMutex;
    std::condition_variable gateCond;
    bool released = false;
    bool firstEntered = false;

    EXPECT_CALL(*m_mockOutput, isOpen())
            .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_mockOutput, writeShort(_))
            .WillRepeatedly(Invoke([&](int32_t word) {
                {
                    std::unique_lock<std::mutex> lock(gateMutex);
                    if (!firstEntered) {
                        // Park the worker on the very first message so the flood
                        // below piles up against the queue cap.
                        firstEntered = true;
                        gateCond.notify_all();
                        gateCond.wait(lock, [&] { return released; });
                    }
                }
                recorder.recordShort(word);
                return pmNoError;
            }));

    enableOutputThread();
    startOutputThread();

    m_pController->sendShortMsg(0x90, 0x00, 0x40);
    {
        std::unique_lock<std::mutex> lock(gateMutex);
        ASSERT_TRUE(gateCond.wait_for(lock, std::chrono::seconds(2), [&] {
            return firstEntered;
        }));
    }

    // Flood far past the 1024-message cap while the worker is parked.
    const int flood = 1100;
    for (int i = 0; i < flood; ++i) {
        m_pController->sendShortMsg(
                0x90, 0x01, static_cast<unsigned char>(i % 128));
    }

    {
        std::lock_guard<std::mutex> lock(gateMutex);
        released = true;
    }
    gateCond.notify_all();
    stopOutputThread();

    // The cap must have dropped messages: fewer delivered than enqueued, but the
    // bulk (~one full queue) still made it through. The exact cap is internal, so
    // we only assert the bound held rather than a precise count.
    const int delivered = recorder.total();
    EXPECT_LT(delivered, flood + 1) << "queue did not drop anything";
    EXPECT_GE(delivered, 1024) << "queue dropped far too much";
    EXPECT_LE(delivered, 1025) << "queue exceeded its cap";
};
