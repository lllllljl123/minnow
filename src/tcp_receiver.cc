#include "tcp_receiver.hh"
#include <numeric>

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // Your code here.
  (void)message;
  // 如果收到 RST，则直接标记错误并返回
  if (message.RST) {
    reassembler_.reader().set_error();
    return;
  }

  // 如果连接尚未建立且消息不包含 SYN，直接返回
  if (!connected_ && !message.SYN) {
    return;
  }

  // 如果消息包含 SYN，初始化连接
  if (message.SYN) {
    connected_ = true;
    zero_point_ = message.seqno;
  }

  // 计算流索引，SYN 不参与 payload 插入
  uint64_t abs_seqno = message.seqno.unwrap(*zero_point_, reassembler_.writer().bytes_pushed());
  stream_index_ = abs_seqno - 1 + message.SYN;

  // 将数据插入到重组器中
  reassembler_.insert(stream_index_, std::move(message.payload), message.FIN);

  // 更新 ACK 值（已接收字节 +1 表示下一个期望的字节，若 FIN 则再 +1）
  ack_ = reassembler_.writer().bytes_pushed() + 1 + reassembler_.writer().is_closed();
}

TCPReceiverMessage TCPReceiver::send() const
{
  // Your code here.
  return TCPReceiverMessage {
    zero_point_.has_value() ? Wrap32::wrap( ack_, *zero_point_ ) : std::optional<Wrap32> {},
    static_cast<uint16_t>( std::min( static_cast<uint64_t>( std::numeric_limits<uint16_t>::max() ),
                                     reassembler_.writer().available_capacity() ) ),
    reassembler_.writer().has_error() };
}
