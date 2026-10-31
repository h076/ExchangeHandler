#pragma once

#include <cstdint>
#include <string>

enum class decode_status
{
  ok,
  ignored,
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
    decode_status decode(const std::string& payload)
    {
      const auto status = do_decode(payload);
      record(status);
      return status;
    }

    const decode_stats& stats() const { return m_stats; }

  protected:

    // Each venue overrides
    virtual decode_status do_decode(const std::string& payload) = 0;

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
