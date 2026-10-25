#include "reconnect_policy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

reconnect_policy::reconnect_policy(reconnect_config c, std::uint64_t seed)
  : cfg(std::move(c)), rng(seed)
{
  // Sanitise config so the maths below can't shrink, go negative or produce NaN
  cfg.multiplier    = std::isfinite(cfg.multiplier) ? std::max(cfg.multiplier, 1.0) : 1.0;
  cfg.jitter        = std::isfinite(cfg.jitter) ? std::clamp(cfg.jitter, 0.0, 1.0) : 0.0;
  cfg.initial_delay = std::max(cfg.initial_delay, duration::zero());
  cfg.max_delay     = std::max(cfg.max_delay, duration::zero());
}

std::optional<reconnect_policy::duration> reconnect_policy::next_delay()
{
  if (cfg.max_attempts != 0 && attempt >= cfg.max_attempts)
  {
    return std::nullopt;
  }

  const std::uint32_t n = attempt;

  // Saturate rather than wrap when retrying forever
  if (attempt < std::numeric_limits<std::uint32_t>::max())
  {
    ++attempt;
  }

  // First retry is immediate
  if (n == 0)
  {
    return duration::zero();
  }

  return apply_jitter(base_delay(n - 1));
}

void reconnect_policy::on_connected(clock::time_point now)
{
  connected_at = now;
}

void reconnect_policy::on_connection_lost(clock::time_point now)
{
  if (connected_at && now - *connected_at >= cfg.stable_after)
  {
    attempt = 0;
  }

  connected_at.reset();
}

void reconnect_policy::reset()
{
  attempt = 0;
  connected_at.reset();
}

reconnect_policy::duration reconnect_policy::base_delay(std::uint32_t exponent) const
{
  // Done in double and capped before converting back: pow() may reach inf for
  // large exponents, which compares correctly against the cap.
  const double max_ms = static_cast<double>(cfg.max_delay.count());
  const double ms = static_cast<double>(cfg.initial_delay.count())
                    * std::pow(cfg.multiplier, static_cast<double>(exponent));

  if (!(ms < max_ms))
  {
    return cfg.max_delay;
  }

  return duration(static_cast<duration::rep>(ms));
}

reconnect_policy::duration reconnect_policy::apply_jitter(duration base)
{
  if (cfg.jitter == 0.0 || base <= duration::zero())
  {
    return base;
  }

  const auto spread = static_cast<duration::rep>(static_cast<double>(base.count()) * cfg.jitter);
  const auto lo = base.count() - spread;

  std::uniform_int_distribution<duration::rep> dist(0, spread);
  return duration(lo + dist(rng));
}
