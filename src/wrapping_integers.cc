#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  // Your code here.
  (void)n;
  (void)zero_point;
  return Wrap32 { zero_point + static_cast<uint32_t>( n % ( 1UL << 32 ) ) };
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // Your code here.
  (void)zero_point;
  (void)checkpoint;
  if ( raw_value_ >= checkpoint ) {
    return raw_value_ - zero_point.raw_value_;
  }
  auto wrapped_checkpoint = Wrap32::wrap( checkpoint, zero_point ).raw_value_;
  uint32_t counter_clockwise = wrapped_checkpoint - raw_value_;
  uint32_t clockwise = raw_value_ - wrapped_checkpoint;
  if ( counter_clockwise < clockwise ) {
    return checkpoint - counter_clockwise;
  }
  return {checkpoint + clockwise};
}
