#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cstdint>
#include <functional>
#include <list>
#include <queue>
#include <optional>

class RetransmissionTimer
{
public:
  RetransmissionTimer(uint64_t initial_RTO_ms) : RTO_(initial_RTO_ms) {}

  bool is_expired() const noexcept { return is_active_ && time_passed_ >= RTO_; }
  bool is_active() const noexcept { return is_active_; }
  RetransmissionTimer& active() noexcept;
  RetransmissionTimer& timeout() noexcept;
  RetransmissionTimer& reset() noexcept;
  RetransmissionTimer& tick(uint64_t ms_since_last_tick) noexcept;

private:
  uint64_t RTO_;
  uint64_t time_passed_ = 0;
  bool is_active_ = false;
};

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender(ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms)
    : input_(std::move(input)), isn_(isn), initial_RTO_ms_(initial_RTO_ms),
      syn_flag_(false), fin_flag_(false), sent_syn_(false), sent_fin_(false),
      timer_(initial_RTO_ms), outstanding_bytes_(), num_bytes_in_flight_(0) {}

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive(const TCPReceiverMessage& msg);

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void(const TCPSenderMessage&)>;

  /* Push bytes from the outbound stream */
  void push(const TransmitFunction& transmit);

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick(uint64_t ms_since_last_tick, const TransmitFunction& transmit);

  // Accessors
  uint64_t sequence_numbers_in_flight() const;
  uint64_t consecutive_retransmissions() const;
  Writer& writer() { return input_.writer(); }
  const Writer& writer() const { return input_.writer(); }

  // Access input stream reader, but const-only (can't read from outside)
  const Reader& reader() const { return input_.reader(); }

private:
  TCPSenderMessage make_message(uint64_t seqno, std::string payload, bool SYN, bool FIN = false) const;

  // Variables initialized in constructor
  ByteStream input_;
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;
  uint64_t wnd_size_ = 1;
  uint64_t next_seqno_ = 0;
  uint64_t acked_seqno_ = 0;
  
  bool syn_flag_;      // 是否已发送 SYN
  bool fin_flag_;      // 是否已发送 FIN
  bool sent_syn_;      // 是否已发送过 SYN
  bool sent_fin_;      // 是否已发送过 FIN

  RetransmissionTimer timer_;
  uint64_t retransmission_cnt_ = 0;
  std::queue<TCPSenderMessage> outstanding_bytes_;
  uint64_t num_bytes_in_flight_;
};
