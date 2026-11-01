#pragma once

#include <cstdint>
#include <string>

#include "../common/received_message.hpp"

enum class decode_status
{
  ok,
  ignored,       // valid but not forwarded: heartbeat, status
  malformed,
  unknown_type
};

struct decode_stats
{
  uint64_t ok = 0;
  uint64_t ignored = 0;
  uint64_t malformed = 0;
  uint64_t unknown_type = 0;
};

class decoder
{
  public:

    virtual ~decoder() = default;
  
    // public entry point 
    decode_status decode(const received_message& msg)
    {
      const auto status = do_decode(msg);
      record(status);
      return status;
    }

    const decode_stats& stats() const { return m_stats; }

  protected:

    // Each venue overrides. Gets the whole message so the receive metadata
    // (recv_time, connection_epoch) can be attached to what it produces.
    virtual decode_status do_decode(const received_message& msg) = 0;

  private:

    void record(decode_status s)
    {
      switch (s)
      {
        case decode_status::ok:
        {
          ++m_stats.ok;
          break;
        }
        case decode_status::ignored:
        {
          ++m_stats.ignored;
          break;
        }
        case decode_status::malformed:
        {
          ++m_stats.malformed;
          break;
        }
        case decode_status::unknown_type:
        {
          ++m_stats.unknown_type;
          break;
        }
      }
    }

    decode_stats m_stats;
};
