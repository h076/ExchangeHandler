#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <random>

struct reconnect_config
{
  using duration = std::chrono::milliseconds;

  duration      initial_delay{250};      // delay before the 2nd retry (the 1st is immediate)
  duration      max_delay{30'000};       // cap on exponential growth
  double        multiplier{2.0};         // growth per failed attempt, clamped to >= 1
  double        jitter{0.5};             // 0 = none, 0.5 = "equal jitter", 1 = "full jitter"
  std::uint32_t max_attempts{0};         // 0 = retry forever
  duration      stable_after{30'000};    // uptime required before backoff resets
};

// Pure backoff logic: no timers, no clock reads. The caller owns the timer and
// passes time points in, which keeps this deterministic and unit-testable.
//
// Delay schedule (before jitter) with defaults:
//   0, 250ms, 500ms, 1s, 2s, 4s, 8s, 16s, 30s, 30s, ...
// The first retry is always immediate and is never jittered.
class reconnect_policy
{
  public:
    using clock    = std::chrono::steady_clock;
    using duration = reconnect_config::duration;

    // Seed is injectable so tests are deterministic
    explicit reconnect_policy(reconnect_config cfg = {},
                              std::uint64_t seed = std::random_device{}());

    // A connect attempt failed or a live session dropped.
    // Returns the delay before the next attempt, or nullopt to give up.
    // Each call consumes one attempt.
    std::optional<duration> next_delay();

    // The session reached connected. Records the time for the stability check.
    void on_connected(clock::time_point now);

    // A live session dropped. If it was up for >= stable_after the attempt
    // counter resets, otherwise (flapping) backoff keeps growing.
    void on_connection_lost(clock::time_point now);

    // Clear all state, e.g. after an intentional disconnect
    void reset();

    std::uint32_t attempts() const { return attempt; }
    const reconnect_config& config() const { return cfg; }

  private:
    duration base_delay(std::uint32_t exponent) const;  // min(max, initial * multiplier^exponent)
    duration apply_jitter(duration base);               // uniform in [base * (1 - jitter), base]

  private:
    reconnect_config cfg;
    std::uint32_t attempt{0};
    std::optional<clock::time_point> connected_at;
    std::mt19937_64 rng;
};
