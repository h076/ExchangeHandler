#include <iostream>
#include <vector>
#include <string>
#include <functional>

#include "subscription_manager.hpp"
#include "../check.hpp"

static void basic_usage()
{
  auto json_formatter = [](const channel_id& channel) -> std::string {
    return R"({"method": "subscribe", "channel": ")" + channel.id + R"("})";
  };

  subscription_manager mgr(json_formatter);
  std::vector<channel_id> channels = {
    channel_id{"BTC-USD"},
    channel_id{"ETH-USD"},
    channel_id{"SOL-USD"}
  };
  mgr.set_channels(channels);

  auto messages = mgr.get_subscription_messages();
  CHECK(messages.size() == 3);
  if (messages.size() != 3) return;
  CHECK(messages[0] == R"({"method": "subscribe", "channel": "BTC-USD"})");
  CHECK(messages[1] == R"({"method": "subscribe", "channel": "ETH-USD"})");
  CHECK(messages[2] == R"({"method": "subscribe", "channel": "SOL-USD"})");
}

static void empty_channels()
{
  auto formatter = [](const channel_id&) -> std::string {
    return "{}";
  };

  subscription_manager mgr(formatter);
  mgr.set_channels({});

  CHECK(mgr.get_subscription_messages().empty());
}

static void update_channels()
{
  auto formatter = [](const channel_id& channel) -> std::string {
    return "sub:" + channel.id;
  };

  subscription_manager mgr(formatter);
  mgr.set_channels({channel_id{"A"}, channel_id{"B"}});
  CHECK((mgr.get_subscription_messages() == std::vector<std::string>{"sub:A", "sub:B"}));

  mgr.set_channels({channel_id{"C"}});
  CHECK((mgr.get_subscription_messages() == std::vector<std::string>{"sub:C"}));
}

static void null_formatter_returns_empty()
{
  subscription_manager mgr(nullptr);
  mgr.set_channels({channel_id{"TEST"}});

  CHECK(mgr.get_subscription_messages().empty());
}

int main()
{
  std::cout << "Running SubscriptionManager tests...\n\n";

  test::run("BasicUsage", basic_usage);
  test::run("EmptyChannels", empty_channels);
  test::run("UpdateChannels", update_channels);
  test::run("NullFormatterReturnsEmpty", null_formatter_returns_empty);

  return test::summary();
}
