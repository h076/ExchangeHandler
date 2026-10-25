#include <iostream>
#include <vector>
#include <chrono>
#include <optional>

#include "reconnect_policy.hpp"
#include "../check.hpp"


using namespace std::chrono_literals;
using ms = std::chrono::milliseconds;
using clock_t_ = reconnect_policy::clock;

static reconnect_config no_jitter()
{
  reconnect_config c;
  c.jitter = 0.0;
  return c;
}

// ---------------------------------------------------------------------------

static void first_retry_is_immediate()
{
  // Immediate even with full jitter
  reconnect_config c;
  c.jitter = 1.0;
  reconnect_policy p(c, 42);
  CHECK(p.next_delay() == ms(0));
  CHECK(p.attempts() == 1);
}

static void exponential_schedule_without_jitter()
{
  reconnect_policy p(no_jitter(), 1);
  const std::vector<ms> expected = {
    0ms, 250ms, 500ms, 1000ms, 2000ms, 4000ms, 8000ms, 16000ms, 30000ms, 30000ms, 30000ms
  };
  for (auto e : expected)
  {
    CHECK(p.next_delay() == e);
  }
}

static void custom_multiplier()
{
  reconnect_config c = no_jitter();
  c.initial_delay = 100ms;
  c.multiplier = 3.0;
  c.max_delay = 1000ms;
  reconnect_policy p(c, 1);
  for (auto e : {0ms, 100ms, 300ms, 900ms, 1000ms})
  {
    CHECK(p.next_delay() == e);
  }
}

static void jitter_stays_in_bounds()
{
  for (double j : {0.25, 0.5, 1.0})
  {
    reconnect_config c;
    c.jitter = j;
    reconnect_policy jittered(c, 7);
    reconnect_policy reference(no_jitter(), 7);

    for (int i = 0; i < 200; ++i)
    {
      auto d = jittered.next_delay();
      auto base = reference.next_delay();
      CHECK(d && base);
      if (!d || !base) return;

      const auto lo = base->count() - static_cast<ms::rep>(base->count() * j);
      CHECK(d->count() >= lo);
      CHECK(*d <= *base);
    }
  }
}

static void jitter_actually_varies()
{
  // At the cap with 0.5 jitter, 50 draws should not all be identical
  reconnect_policy p({}, 3);
  for (int i = 0; i < 10; ++i) p.next_delay();   // reach the 30s cap

  auto first = p.next_delay();
  bool varied = false;
  for (int i = 0; i < 50; ++i)
  {
    if (p.next_delay() != first) varied = true;
  }
  CHECK(varied);
}

static void same_seed_is_deterministic()
{
  reconnect_policy a({}, 1234);
  reconnect_policy b({}, 1234);
  for (int i = 0; i < 50; ++i)
  {
    CHECK(a.next_delay() == b.next_delay());
  }
}

static void max_attempts_gives_up()
{
  reconnect_config c = no_jitter();
  c.max_attempts = 3;
  reconnect_policy p(c, 1);

  CHECK(p.next_delay() == ms(0));
  CHECK(p.next_delay() == ms(250));
  CHECK(p.next_delay() == ms(500));
  CHECK(!p.next_delay().has_value());
  CHECK(!p.next_delay().has_value());   // stays exhausted
  CHECK(p.attempts() == 3);
}

static void stable_connection_resets_backoff()
{
  reconnect_policy p(no_jitter(), 1);
  for (int i = 0; i < 5; ++i) p.next_delay();

  const auto t0 = clock_t_::time_point{} + 1h;
  p.on_connected(t0);
  p.on_connection_lost(t0 + 30s);   // exactly stable_after counts as stable

  CHECK(p.attempts() == 0);
  CHECK(p.next_delay() == ms(0));
  CHECK(p.next_delay() == ms(250));
}

static void flapping_connection_keeps_backing_off()
{
  reconnect_policy p(no_jitter(), 1);
  for (int i = 0; i < 4; ++i) p.next_delay();   // 0, 250, 500, 1000

  const auto t0 = clock_t_::time_point{} + 1h;
  p.on_connected(t0);
  p.on_connection_lost(t0 + 29'999ms);

  CHECK(p.attempts() == 4);
  CHECK(p.next_delay() == ms(2000));
}

static void lost_without_connected_does_not_reset()
{
  reconnect_policy p(no_jitter(), 1);
  for (int i = 0; i < 3; ++i) p.next_delay();

  const auto t0 = clock_t_::time_point{} + 1h;
  p.on_connection_lost(t0 + 1h);    // never connected
  CHECK(p.attempts() == 3);

  // connected_at is consumed by a loss: a second loss can't reuse it
  p.on_connected(t0);
  p.on_connection_lost(t0 + 1s);
  p.on_connection_lost(t0 + 1h);
  CHECK(p.attempts() == 3);
}

static void reset_clears_state()
{
  reconnect_config c = no_jitter();
  c.max_attempts = 2;
  reconnect_policy p(c, 1);
  p.next_delay();
  p.next_delay();
  CHECK(!p.next_delay().has_value());

  p.reset();
  CHECK(p.attempts() == 0);
  CHECK(p.next_delay() == ms(0));
  CHECK(p.next_delay() == ms(250));
}

static void many_attempts_do_not_overflow()
{
  reconnect_policy p(no_jitter(), 1);
  std::optional<ms> last;
  for (int i = 0; i < 100'000; ++i) last = p.next_delay();
  CHECK(last == ms(30'000));
}

static void config_is_sanitised()
{
  // initial above the cap clamps to the cap
  {
    reconnect_config c = no_jitter();
    c.initial_delay = 60s;
    reconnect_policy p(c, 1);
    CHECK(p.next_delay() == ms(0));
    CHECK(p.next_delay() == ms(30'000));
  }
  // out-of-range multiplier / jitter are clamped
  {
    reconnect_config c;
    c.multiplier = 0.5;
    c.jitter = 5.0;
    reconnect_policy p(c, 1);
    CHECK(p.config().multiplier == 1.0);
    CHECK(p.config().jitter == 1.0);
  }
}

int main()
{
  std::cout << "Running reconnect_policy tests...\n\n";

  test::run("FirstRetryIsImmediate", first_retry_is_immediate);
  test::run("ExponentialScheduleWithoutJitter", exponential_schedule_without_jitter);
  test::run("CustomMultiplier", custom_multiplier);
  test::run("JitterStaysInBounds", jitter_stays_in_bounds);
  test::run("JitterActuallyVaries", jitter_actually_varies);
  test::run("SameSeedIsDeterministic", same_seed_is_deterministic);
  test::run("MaxAttemptsGivesUp", max_attempts_gives_up);
  test::run("StableConnectionResetsBackoff", stable_connection_resets_backoff);
  test::run("FlappingConnectionKeepsBackingOff", flapping_connection_keeps_backing_off);
  test::run("LostWithoutConnectedDoesNotReset", lost_without_connected_does_not_reset);
  test::run("ResetClearsState", reset_clears_state);
  test::run("ManyAttemptsDoNotOverflow", many_attempts_do_not_overflow);
  test::run("ConfigIsSanitised", config_is_sanitised);

  return test::summary();
}
