#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>

#include "kraken_parse.hpp"
#include "../check.hpp"

using kraken::fixed;
namespace kp = kraken::parse;

static std::optional<fixed> fx(std::string_view s)
{
  fixed v{};
  if (!kp::fixed_from_text(s, v)) return std::nullopt;
  return v;
}

static std::optional<std::int64_t> ts(std::string_view s)
{
  std::int64_t v{};
  if (!kp::timestamp_ns(s, v)) return std::nullopt;
  return v;
}

// ---------------------------------------------------------------------------

static void fixed_plain_decimals()
{
  CHECK(fx("65001.5") == 6500150000000);
  CHECK(fx("65000.0") == 6500000000000);
  CHECK(fx("65000") == 6500000000000);
  CHECK(fx("0") == 0);
  CHECK(fx("0.8") == 80000000);
  CHECK(fx("0.00000001") == 1);
  CHECK(fx("-1.5") == -150000000);
  CHECK(fx("007.25") == 725000000);
}

static void fixed_is_exact_where_double_is_not()
{
  // 0.1 + 0.2 style values: exact in fixed point
  CHECK(fx("0.1") == 10000000);
  CHECK(fx("0.3") == 30000000);
  CHECK(fx("12345678.12345678") == 1234567812345678);
}

static void fixed_exponent_notation()
{
  CHECK(fx("1e-05") == 1000);
  CHECK(fx("1E-8") == 1);
  CHECK(fx("1.5e3") == 150000000000);
  CHECK(fx("2.5E+2") == 25000000000);
  CHECK(fx("1e-9") == std::nullopt);       // below 1e-8
  CHECK(fx("10e-9") == 1);                 // exactly 1e-8
}

static void fixed_precision_limit()
{
  CHECK(fx("1.12345678") == 112345678);
  CHECK(fx("1.123456789") == std::nullopt);       // 9th decimal would be lost
  CHECK(fx("1.1234567800") == 112345678);         // extra zeros are fine
}

static void fixed_range_limit()
{
  CHECK(fx("92233720368.54775807") == std::numeric_limits<fixed>::max());
  CHECK(fx("92233720368.54775808") == std::nullopt);
  CHECK(fx("1e20") == std::nullopt);
}

static void fixed_rejects_garbage()
{
  CHECK(fx("") == std::nullopt);
  CHECK(fx("-") == std::nullopt);
  CHECK(fx(".") == std::nullopt);
  CHECK(fx("abc") == std::nullopt);
  CHECK(fx("1.2.3") == std::nullopt);
  CHECK(fx("1e") == std::nullopt);
  CHECK(fx("12x") == std::nullopt);
  CHECK(fx("1.5 ") == 150000000);          // trailing whitespace (simdjson raw tokens)
}

static void timestamp_with_fraction()
{
  CHECK(ts("2023-10-06T17:35:55.440295Z") == 1696613755440295000);
  CHECK(ts("2023-10-06T17:35:55.1Z") == 1696613755100000000);
  CHECK(ts("2023-10-06T17:35:55.123456789Z") == 1696613755123456789);
  CHECK(ts("2023-10-06T17:35:55.1234567891Z") == 1696613755123456789);   // truncated
}

static void timestamp_without_fraction()
{
  CHECK(ts("1970-01-01T00:00:00Z") == 0);
  CHECK(ts("2023-10-06T17:35:55Z") == 1696613755000000000);
  CHECK(ts("2024-02-29T00:00:00Z") == 1709164800000000000);   // leap day
}

static void timestamp_rejects_bad_input()
{
  CHECK(ts("") == std::nullopt);
  CHECK(ts("2023-10-06 17:35:55Z") == std::nullopt);    // space instead of T
  CHECK(ts("2023-10-06T17:35:55") == std::nullopt);     // no Z
  CHECK(ts("2023-10-06T17:35:55.Z") == std::nullopt);   // empty fraction
  CHECK(ts("2023-13-06T17:35:55Z") == std::nullopt);    // month 13
  CHECK(ts("2023-02-30T17:35:55Z") == std::nullopt);    // Feb 30
  CHECK(ts("2023-10-06T24:00:00Z") == std::nullopt);
  CHECK(ts("2023-10-06T17:35:55Zjunk") == std::nullopt);
}

static void symbol_limits()
{
  kraken::symbol s;
  CHECK(kp::symbol_from("BTC/USD", s));
  CHECK(s.view() == "BTC/USD");
  CHECK(kp::symbol_from("ABCDEFGH/IJKLMNO", s));      // 16 chars: fits exactly
  CHECK(s.view().size() == 16);
  CHECK(!kp::symbol_from("ABCDEFGH/IJKLMNOP", s));    // 17 chars
  CHECK(!kp::symbol_from("", s));

  // Shorter symbol after a longer one leaves no stale characters
  CHECK(kp::symbol_from("ETH/USD", s));
  CHECK(s.view() == "ETH/USD");
  CHECK(s.chars[7] == '\0');
}

int main()
{
  std::cout << "Running kraken_parse tests...\n\n";

  test::run("FixedPlainDecimals", fixed_plain_decimals);
  test::run("FixedIsExactWhereDoubleIsNot", fixed_is_exact_where_double_is_not);
  test::run("FixedExponentNotation", fixed_exponent_notation);
  test::run("FixedPrecisionLimit", fixed_precision_limit);
  test::run("FixedRangeLimit", fixed_range_limit);
  test::run("FixedRejectsGarbage", fixed_rejects_garbage);
  test::run("TimestampWithFraction", timestamp_with_fraction);
  test::run("TimestampWithoutFraction", timestamp_without_fraction);
  test::run("TimestampRejectsBadInput", timestamp_rejects_bad_input);
  test::run("SymbolLimits", symbol_limits);

  return test::summary();
}
