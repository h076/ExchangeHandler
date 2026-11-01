#pragma once

// Field-level parsers for Kraken v2 messages, used by kraken_decoder.
// All return false on bad input; nothing allocates except vector growth.

#include <chrono>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include <simdjson.h>

#include "../common/kraken_messages.hpp"
#include "../common/market_types.hpp"

namespace kraken::parse {

  // General types from common/market_types.hpp
  using mb::fixed;
  using mb::fixed_decimals;
  using mb::level;
  using mb::symbol;

  // Decimal text -> fixed point, exactly (no double rounding).
  // Accepts JSON number syntax: "65001.5", "-1", "0.00000001", "1e-05", "1.5E+3".
  // Fails if the value needs more than 8 decimal places (unless the extra digits
  // are zero), overflows int64, or has trailing non-whitespace.
  inline bool fixed_from_text(std::string_view s, fixed& out)
  {
    std::size_t i = 0;
    const bool negative = i < s.size() && s[i] == '-';
    if (negative) 
    {
      ++i;
    }

    // Mantissa: all digits, remembering how many came after the '.'
    std::uint64_t mantissa = 0;
    int frac_digits = 0;
    bool any_digit = false;
    bool seen_dot = false;
    for (; i < s.size(); ++i)
    {
      const char c = s[i];
      if (c >= '0' && c <= '9')
      {
        if (mantissa > (std::numeric_limits<std::uint64_t>::max() - 9) / 10) return false;
        mantissa = mantissa * 10 + static_cast<std::uint64_t>(c - '0');
        any_digit = true;
        if (seen_dot) ++frac_digits;
      }
      else if (c == '.' && !seen_dot)
      {
        seen_dot = true;
      }
      else
      {
        break;
      }
    }

    if (!any_digit) 
    {
      return false;
    }

    // Optional exponent
    int exponent = 0;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
    {
      ++i;
      bool exp_negative = false;

      if (i < s.size() && (s[i] == '+' || s[i] == '-'))
      {
        exp_negative = s[i] == '-';
        ++i;
      }

      bool exp_digit = false;
      for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i)
      {
        exponent = exponent * 10 + (s[i] - '0');
        if (exponent > 400) return false;
        exp_digit = true;
      }

      if (!exp_digit) 
      {
        return false;
      }

      if (exp_negative) 
      {
        exponent = -exponent;
      }
    }

    // simdjson's raw tokens may carry trailing whitespace; anything else is an error
    for (; i < s.size(); ++i)
    {
      if (s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') 
      {
        return false;
      }
    }

    // value = mantissa * 10^(exponent - frac_digits); scale by 10^8 into fixed
    int shift = fixed_decimals + exponent - frac_digits;
    for (; shift > 0; --shift)
    {
      if (mantissa > std::numeric_limits<std::uint64_t>::max() / 10) 
      {
        return false;
      }

      mantissa *= 10;
    }
    for (; shift < 0; ++shift)
    {
      if (mantissa % 10 != 0) 
      {
        return false;   // would lose precision
      }

      mantissa /= 10;
    }

    if (mantissa > static_cast<std::uint64_t>(std::numeric_limits<fixed>::max())) 
    {
      return false;
    }

    out = negative ? -static_cast<fixed>(mantissa) : static_cast<fixed>(mantissa);
    return true;
  }

  // A JSON number (or numeric string) -> fixed
  inline bool fixed_from_json(simdjson::ondemand::value& v, fixed& out)
  {
    simdjson::ondemand::json_type type;
    if (v.type().get(type)) 
    {
      return false;
    }

    if (type == simdjson::ondemand::json_type::number)
    {
      return fixed_from_text(v.raw_json_token(), out);
    }

    if (type == simdjson::ondemand::json_type::string)
    {
      std::string_view text;
      return !v.get_string().get(text) && fixed_from_text(text, out);
    }

    return false;
  }

  namespace detail {

    // Exactly n digits at s[pos], advancing pos
    inline bool digits(std::string_view s, std::size_t& pos, int n, int& out)
    {
      if (pos + static_cast<std::size_t>(n) > s.size())
      {
        return false;
      }

      out = 0;
      for (int k = 0; k < n; ++k, ++pos)
      {
        const char c = s[pos];
        if (c < '0' || c > '9') return false;
        out = out * 10 + (c - '0');
      }

      return true;
    }

    inline bool expect(std::string_view s, std::size_t& pos, char c)
    {
      if (pos >= s.size() || s[pos] != c) 
      {
        return false;
      }

      ++pos;
      return true;
    }

  } // namespace detail

  // RFC 3339 UTC timestamp -> nanoseconds since the Unix epoch.
  // Format: YYYY-MM-DDTHH:MM:SS[.fraction]Z, fraction up to 9 digits (extra digits truncated).
  inline bool timestamp_ns(std::string_view s, std::int64_t& out)
  {
    using detail::digits;
    using detail::expect;

    std::size_t p = 0;
    int y, mo, d, h, mi, sec;
    if (!digits(s, p, 4, y)  || !expect(s, p, '-') ||
        !digits(s, p, 2, mo) || !expect(s, p, '-') ||
        !digits(s, p, 2, d)  || !expect(s, p, 'T') ||
        !digits(s, p, 2, h)  || !expect(s, p, ':') ||
        !digits(s, p, 2, mi) || !expect(s, p, ':') ||
        !digits(s, p, 2, sec))
    {
      return false;
    }

    std::int64_t frac_ns = 0;
    if (p < s.size() && s[p] == '.')
    {
      ++p;
      int n = 0;
      for (; p < s.size() && s[p] >= '0' && s[p] <= '9'; ++p, ++n)
      {
        if (n < 9) frac_ns = frac_ns * 10 + (s[p] - '0');
      }
      if (n == 0) return false;
      for (; n < 9; ++n) frac_ns *= 10;
    }

    if (!expect(s, p, 'Z') || p != s.size()) 
    {
      return false;
    }

    const std::chrono::year_month_day ymd{
        std::chrono::year{y}, std::chrono::month{static_cast<unsigned>(mo)},
        std::chrono::day{static_cast<unsigned>(d)}};

    if (!ymd.ok() || h > 23 || mi > 59 || sec > 60)   // 60: leap second
    {
      return false;
    }

    const std::int64_t days = std::chrono::sys_days{ymd}.time_since_epoch().count();
    const std::int64_t secs = days * 86'400 + h * 3'600 + mi * 60 + sec;
    out = secs * 1'000'000'000 + frac_ns;
    return true;
  }

  // "BTC/USD" -> fixed-size symbol (fails if empty or longer than the buffer)
  inline bool symbol_from(std::string_view s, symbol& out)
  {
    if (s.empty() || s.size() > out.chars.size())
    {
      return false;
    }

    out.chars = {};
    for (std::size_t k = 0; k < s.size(); ++k) 
    {
      out.chars[k] = s[k];
    }

    out.len = static_cast<std::uint8_t>(s.size());

    return true;
  }

  // [ { "price": ..., "qty": ... }, ... ] -> levels (appended to out)
  inline bool levels(simdjson::ondemand::array& arr, std::vector<level>& out)
  {
    for (auto element : arr)
    {
      simdjson::ondemand::object obj;
      if (element.get_object().get(obj))
      {
        return false;
      }

      simdjson::ondemand::value price, qty;
      level lvl{};
      if (obj["price"].get(price) || !fixed_from_json(price, lvl.price)) 
      {
        return false;
      }

      if (obj["qty"].get(qty) || !fixed_from_json(qty, lvl.qty))
      {
        return false;
      }

      out.push_back(lvl);
    }
    return true;
  }

  // Optional side: a missing "bids"/"asks" field means no levels on that side
  inline bool side(simdjson::ondemand::object& obj, std::string_view key, std::vector<level>& out)
  {
    simdjson::ondemand::array arr;
    const auto err = obj[key].get_array().get(arr);
    if (err == simdjson::NO_SUCH_FIELD) return true;
    if (err) return false;
    return levels(arr, out);
  }

  // One element of a book message's "data" array -> book_snapshot / book_update.
  // Clears and refills out's vectors, so their capacity is reused across messages.
  // Fields are read in Kraken's order (symbol, bids, asks, checksum, timestamp),
  // which is the fast path for simdjson on-demand; other orders still work.
  template <class Book>
  bool book_entry(simdjson::ondemand::object& obj, Book& out)
  {
    out.bids.clear();
    out.asks.clear();

    std::string_view sym;
    if (obj["symbol"].get_string().get(sym) || !symbol_from(sym, out.sym)) 
    {
      return false;
    }

    if (!side(obj, "bids", out.bids)) 
    {
      return false;
    }

    if (!side(obj, "asks", out.asks)) 
    {
      return false;
    }

    std::uint64_t checksum = 0;
    if (obj["checksum"].get_uint64().get(checksum) ||
        checksum > std::numeric_limits<std::uint32_t>::max())
    {
      return false;
    }

    out.checksum = static_cast<std::uint32_t>(checksum);

    // Only book_update carries a timestamp in the internal format
    if constexpr (requires { out.timestamp_ns; })
    {
      std::string_view ts;
      if (obj["timestamp"].get_string().get(ts) || !timestamp_ns(ts, out.timestamp_ns))
      {
        return false;
      }
    }

    return true;
  }

} // namespace kraken::parse
