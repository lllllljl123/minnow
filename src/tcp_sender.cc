#include "tcp_sender.hh"
#include "tcp_config.hh"
#include <algorithm>

using namespace std;

RetransmissionTimer& RetransmissionTimer::active() noexcept
{
  is_active_ = true;
  return *this;
}

RetransmissionTimer& RetransmissionTimer::timeout() noexcept
{
  RTO_ <<= 1; // double the RTO value on timeout
  return *this;
}

RetransmissionTimer& RetransmissionTimer::reset() noexcept
{
  time_passed_ = 0;
  return *this;
}

RetransmissionTimer& RetransmissionTimer::tick(uint64_t ms_since_last_tick) noexcept
{
  time_passed_ += is_active_ ? ms_since_last_tick : 0;
  return *this;
}

void TCPSender::push(const TransmitFunction& transmit)
{
  Reader& bytes_reader = input_.reader();
  fin_flag_ |= bytes_reader.is_finished(); // Allow sending FIN once the stream is finished
  if (sent_fin_) return; // Do nothing if the stream has already ended

  const size_t window_size = wnd_size_ == 0 ? 1 : wnd_size_;
  
  for (string payload{}; num_bytes_in_flight_ < window_size && !sent_fin_; payload.clear()) {
    string_view bytes_view = bytes_reader.peek();
    if (sent_syn_ && bytes_view.empty() && !fin_flag_) break;

    while (payload.size() + num_bytes_in_flight_ + (!sent_syn_) < window_size && payload.size() < TCPConfig::MAX_PAYLOAD_SIZE) {
      if (bytes_view.empty() || fin_flag_) break;

      const uint64_t available_size = min(TCPConfig::MAX_PAYLOAD_SIZE - payload.size(),
                                          window_size - (payload.size() + num_bytes_in_flight_ + (!sent_syn_)));
      if (bytes_view.size() > available_size) {
        bytes_view.remove_suffix(bytes_view.size() - available_size);
      }

      payload.append(bytes_view);
      bytes_reader.pop(bytes_view.size());
      fin_flag_ |= bytes_reader.is_finished();
      bytes_view = bytes_reader.peek();
    }

    auto& msg = outstanding_bytes_.emplace(make_message(next_seqno_, move(payload), sent_syn_ ? syn_flag_ : true, fin_flag_));
    const size_t margin = sent_syn_ ? syn_flag_ : 0;

    if (fin_flag_ && (msg.sequence_length() - margin) + num_bytes_in_flight_ > window_size)
      msg.FIN = false;
    else if (fin_flag_)
      sent_fin_ = true;

    const size_t correct_length = msg.sequence_length() - margin;
    num_bytes_in_flight_ += correct_length;
    next_seqno_ += correct_length;
    sent_syn_ = true;
    transmit(msg);

    if (correct_length != 0)
      timer_.active();
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return make_message(next_seqno_, {}, false);
}

void TCPSender::receive(const TCPReceiverMessage& msg)
{
  wnd_size_ = msg.window_size;
  if (!msg.ackno.has_value()) {
    if (msg.window_size == 0)
      input_.set_error();
    return;
  }

  const uint64_t expected_seqno = msg.ackno->unwrap(isn_, next_seqno_);
  if (expected_seqno > next_seqno_) return;

  bool is_acknowledged = false;
  while (!outstanding_bytes_.empty()) {
    auto& buffered_msg = outstanding_bytes_.front();
    const uint64_t final_seqno = acked_seqno_ + buffered_msg.sequence_length() - buffered_msg.SYN;
    if (expected_seqno <= acked_seqno_ || expected_seqno < final_seqno)
      break;

    is_acknowledged = true;
    num_bytes_in_flight_ -= buffered_msg.sequence_length() - syn_flag_;
    acked_seqno_ += buffered_msg.sequence_length() - syn_flag_;
    syn_flag_ = sent_syn_ ? syn_flag_ : expected_seqno <= next_seqno_;
    outstanding_bytes_.pop();
  }

  if (is_acknowledged) {
    if (outstanding_bytes_.empty())
      timer_ = RetransmissionTimer(initial_RTO_ms_);
    else
      timer_ = move(RetransmissionTimer(initial_RTO_ms_).active());
    retransmission_cnt_ = 0;
  }
}

void TCPSender::tick(uint64_t ms_since_last_tick, const TransmitFunction& transmit)
{
  if (timer_.tick(ms_since_last_tick).is_expired()) {
    transmit(outstanding_bytes_.front());
    if (wnd_size_ == 0)
      timer_.reset();
    else
      timer_.timeout().reset();
    ++retransmission_cnt_;
  }
}

TCPSenderMessage TCPSender::make_message(uint64_t seqno, string payload, bool SYN, bool FIN) const
{
  return { .seqno = Wrap32::wrap(seqno, isn_),
           .SYN = SYN,
           .payload = move(payload),
           .FIN = FIN,
           .RST = input_.reader().has_error() };
}

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return num_bytes_in_flight_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return retransmission_cnt_;
}
