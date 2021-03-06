/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/system_wrappers/interface/atomic32.h"
#include "webrtc/system_wrappers/interface/critical_section_wrapper.h"
#include "webrtc/system_wrappers/interface/event_wrapper.h"
#include "webrtc/test/testsupport/fileutils.h"
#include "webrtc/voice_engine/test/auto_test/fixtures/after_streaming_fixture.h"
#include "webrtc/voice_engine/test/auto_test/voe_standard_test.h"

class TestRtpObserver : public webrtc::VoERTPObserver {
 public:
  TestRtpObserver()
      : crit_(voetest::CriticalSectionWrapper::CreateCriticalSection()),
        changed_ssrc_event_(voetest::EventWrapper::Create()) {}
  virtual ~TestRtpObserver() {}
  virtual void OnIncomingCSRCChanged(int channel,
                                     unsigned int CSRC,
                                     bool added) {}
  virtual void OnIncomingSSRCChanged(int channel,
                                     unsigned int SSRC);
  void WaitForChangedSsrc() {
    // 10 seconds should be enough.
    EXPECT_EQ(voetest::kEventSignaled, changed_ssrc_event_->Wait(10*1000));
    changed_ssrc_event_->Reset();
  }
  void SetIncomingSsrc(unsigned int ssrc) {
    voetest::CriticalSectionScoped lock(crit_.get());
    incoming_ssrc_ = ssrc;
  }
 public:
  voetest::scoped_ptr<voetest::CriticalSectionWrapper> crit_;
  unsigned int incoming_ssrc_;
  voetest::scoped_ptr<voetest::EventWrapper> changed_ssrc_event_;
};

void TestRtpObserver::OnIncomingSSRCChanged(int channel,
                                            unsigned int SSRC) {
  char msg[128];
  sprintf(msg, "\n=> OnIncomingSSRCChanged(channel=%d, SSRC=%u)\n", channel,
          SSRC);
  TEST_LOG("%s", msg);

  {
    voetest::CriticalSectionScoped lock(crit_.get());
    if (incoming_ssrc_ == SSRC)
      changed_ssrc_event_->Set();
  }
}

class RtcpAppHandler : public webrtc::VoERTCPObserver {
 public:
  RtcpAppHandler() : length_in_bytes_(0), sub_type_(0), name_(0) {}
  void OnApplicationDataReceived(int channel,
                                 unsigned char sub_type,
                                 unsigned int name,
                                 const unsigned char* data,
                                 unsigned short length_in_bytes);
  void Reset();
  ~RtcpAppHandler() {}
  unsigned short length_in_bytes_;
  unsigned char data_[256];
  unsigned char sub_type_;
  unsigned int name_;
};


static const char* const RTCP_CNAME = "Whatever";

class RtpRtcpTest : public AfterStreamingFixture {
 protected:
  void SetUp() {
    // We need a second channel for this test, so set it up.
    second_channel_ = voe_base_->CreateChannel();
    EXPECT_GE(second_channel_, 0);

    transport_ = new LoopBackTransport(voe_network_);
    EXPECT_EQ(0, voe_network_->RegisterExternalTransport(second_channel_,
                                                         *transport_));

    EXPECT_EQ(0, voe_base_->StartReceive(second_channel_));
    EXPECT_EQ(0, voe_base_->StartPlayout(second_channel_));
    EXPECT_EQ(0, voe_rtp_rtcp_->SetLocalSSRC(second_channel_, 5678));
    EXPECT_EQ(0, voe_base_->StartSend(second_channel_));

    // We'll set up the RTCP CNAME and SSRC to something arbitrary here.
    voe_rtp_rtcp_->SetRTCP_CNAME(channel_, RTCP_CNAME);
  }

  void TearDown() {
    EXPECT_EQ(0, voe_network_->DeRegisterExternalTransport(second_channel_));
    voe_base_->DeleteChannel(second_channel_);
    delete transport_;
  }

  int second_channel_;
  LoopBackTransport* transport_;
};

void RtcpAppHandler::OnApplicationDataReceived(
    const int /*channel*/, unsigned char sub_type,
    unsigned int name, const unsigned char* data,
    unsigned short length_in_bytes) {
  length_in_bytes_ = length_in_bytes;
  memcpy(data_, &data[0], length_in_bytes);
  sub_type_ = sub_type;
  name_ = name;
}

void RtcpAppHandler::Reset() {
  length_in_bytes_ = 0;
  memset(data_, 0, sizeof(data_));
  sub_type_ = 0;
  name_ = 0;
}

TEST_F(RtpRtcpTest, RemoteRtcpCnameHasPropagatedToRemoteSide) {
  if (!FLAGS_include_timing_dependent_tests) {
    TEST_LOG("Skipping test - running in slow execution environment...\n");
    return;
  }

  // We need to sleep a bit here for the name to propagate. For instance,
  // 200 milliseconds is not enough, so we'll go with one second here.
  Sleep(1000);

  char char_buffer[256];
  voe_rtp_rtcp_->GetRemoteRTCP_CNAME(channel_, char_buffer);
  EXPECT_STREQ(RTCP_CNAME, char_buffer);
}

// Flakily hangs on Linux. code.google.com/p/webrtc/issues/detail?id=2178.
TEST_F(RtpRtcpTest, DISABLED_ON_LINUX(SSRCPropagatesCorrectly)) {
  unsigned int local_ssrc = 1234;
  EXPECT_EQ(0, voe_base_->StopSend(channel_));
  EXPECT_EQ(0, voe_rtp_rtcp_->SetLocalSSRC(channel_, local_ssrc));
  EXPECT_EQ(0, voe_base_->StartSend(channel_));

  Sleep(1000);

  unsigned int ssrc;
  EXPECT_EQ(0, voe_rtp_rtcp_->GetLocalSSRC(channel_, ssrc));
  EXPECT_EQ(local_ssrc, ssrc);

  EXPECT_EQ(0, voe_rtp_rtcp_->GetRemoteSSRC(channel_, ssrc));
  EXPECT_EQ(local_ssrc, ssrc);
}

TEST_F(RtpRtcpTest, RtcpApplicationDefinedPacketsCanBeSentAndReceived) {
  RtcpAppHandler rtcp_app_handler;
  EXPECT_EQ(0, voe_rtp_rtcp_->RegisterRTCPObserver(
      channel_, rtcp_app_handler));

  // Send data aligned to 32 bytes.
  const char* data = "application-dependent data------";
  unsigned short data_length = strlen(data);
  unsigned int data_name = 0x41424344;  // 'ABCD' in ascii
  unsigned char data_subtype = 1;

  EXPECT_EQ(0, voe_rtp_rtcp_->SendApplicationDefinedRTCPPacket(
      channel_, data_subtype, data_name, data, data_length));

  // Ensure the RTP-RTCP process gets scheduled.
  Sleep(1000);

  // Ensure we received the data in the callback.
  ASSERT_EQ(data_length, rtcp_app_handler.length_in_bytes_);
  EXPECT_EQ(0, memcmp(data, rtcp_app_handler.data_, data_length));
  EXPECT_EQ(data_name, rtcp_app_handler.name_);
  EXPECT_EQ(data_subtype, rtcp_app_handler.sub_type_);

  EXPECT_EQ(0, voe_rtp_rtcp_->DeRegisterRTCPObserver(channel_));
}

TEST_F(RtpRtcpTest, DisabledRtcpObserverDoesNotReceiveData) {
  RtcpAppHandler rtcp_app_handler;
  EXPECT_EQ(0, voe_rtp_rtcp_->RegisterRTCPObserver(
      channel_, rtcp_app_handler));

  // Put observer in a known state before de-registering.
  rtcp_app_handler.Reset();

  EXPECT_EQ(0, voe_rtp_rtcp_->DeRegisterRTCPObserver(channel_));

  const char* data = "whatever";
  EXPECT_EQ(0, voe_rtp_rtcp_->SendApplicationDefinedRTCPPacket(
      channel_, 1, 0x41424344, data, strlen(data)));

  // Ensure the RTP-RTCP process gets scheduled.
  Sleep(1000);

  // Ensure we received no data.
  EXPECT_EQ(0u, rtcp_app_handler.name_);
  EXPECT_EQ(0u, rtcp_app_handler.sub_type_);
}

// TODO(xians, phoglund): Re-enable when issue 372 is resolved.
TEST_F(RtpRtcpTest, DISABLED_CanCreateRtpDumpFilesWithoutError) {
  // Create two RTP dump files (3 seconds long). You can verify these after
  // the test using rtpplay or NetEqRTPplay if you like.
  std::string output_path = webrtc::test::OutputPath();
  std::string incoming_filename = output_path + "dump_in_3sec.rtp";
  std::string outgoing_filename = output_path + "dump_out_3sec.rtp";

  EXPECT_EQ(0, voe_rtp_rtcp_->StartRTPDump(
      channel_, incoming_filename.c_str(), webrtc::kRtpIncoming));
  EXPECT_EQ(0, voe_rtp_rtcp_->StartRTPDump(
      channel_, outgoing_filename.c_str(), webrtc::kRtpOutgoing));

  Sleep(3000);

  EXPECT_EQ(0, voe_rtp_rtcp_->StopRTPDump(channel_, webrtc::kRtpIncoming));
  EXPECT_EQ(0, voe_rtp_rtcp_->StopRTPDump(channel_, webrtc::kRtpOutgoing));
}

TEST_F(RtpRtcpTest, ObserverGetsNotifiedOnSsrcChange) {
  TestRtpObserver rtcp_observer;
  EXPECT_EQ(0, voe_rtp_rtcp_->RegisterRTPObserver(
      channel_, rtcp_observer));

  unsigned int new_ssrc = 7777;
  EXPECT_EQ(0, voe_base_->StopSend(channel_));
  rtcp_observer.SetIncomingSsrc(new_ssrc);
  EXPECT_EQ(0, voe_rtp_rtcp_->SetLocalSSRC(channel_, new_ssrc));
  EXPECT_EQ(0, voe_base_->StartSend(channel_));

  rtcp_observer.WaitForChangedSsrc();

  // Now try another SSRC.
  unsigned int newer_ssrc = 1717;
  EXPECT_EQ(0, voe_base_->StopSend(channel_));
  rtcp_observer.SetIncomingSsrc(newer_ssrc);
  EXPECT_EQ(0, voe_rtp_rtcp_->SetLocalSSRC(channel_, newer_ssrc));
  EXPECT_EQ(0, voe_base_->StartSend(channel_));

  rtcp_observer.WaitForChangedSsrc();

  EXPECT_EQ(0, voe_rtp_rtcp_->DeRegisterRTPObserver(channel_));
}
