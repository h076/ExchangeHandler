#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <functional>

#include "subscription_manager.hpp"

int main()
{
  std::cout << "Running SubscriptionManager tests...\n\n";

  // Test 1: Basic usage
  {
    std::cout << "Test 1: BasicUsage... ";
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
    assert(messages.size() == 3);
    assert(messages[0] == R"({"method": "subscribe", "channel": "BTC-USD"})");
    assert(messages[1] == R"({"method": "subscribe", "channel": "ETH-USD"})");
    assert(messages[2] == R"({"method": "subscribe", "channel": "SOL-USD"})");
    std::cout << "PASSED\n";
  }

  // Test 2: Empty channels
  {
    std::cout << "Test 2: EmptyChannels... ";
    auto formatter = [](const channel_id&) -> std::string {
      return "{}";
    };

    subscription_manager mgr(formatter);
    mgr.set_channels({});

    auto messages = mgr.get_subscription_messages();
    assert(messages.empty());
    std::cout << "PASSED\n";
  }

  // Test 3: Update channels
  {
    std::cout << "Test 3: UpdateChannels... ";
    auto formatter = [](const channel_id& channel) -> std::string {
      return "sub:" + channel.id;
    };

    subscription_manager mgr(formatter);
    mgr.set_channels({channel_id{"A"}, channel_id{"B"}});
    auto msgs1 = mgr.get_subscription_messages();
    assert(msgs1.size() == 2);
    assert(msgs1[0] == "sub:A");
    assert(msgs1[1] == "sub:B");

    mgr.set_channels({channel_id{"C"}});
    auto msgs2 = mgr.get_subscription_messages();
    assert(msgs2.size() == 1);
    assert(msgs2[0] == "sub:C");
    std::cout << "PASSED\n";
  }

  // Test 4: Null formatter returns empty
  {
    std::cout << "Test 4: NullFormatterReturnsEmpty... ";
    subscription_manager mgr(nullptr);
    mgr.set_channels({channel_id{"TEST"}});

    auto messages = mgr.get_subscription_messages();
    assert(messages.empty());
    std::cout << "PASSED\n";
  }

  std::cout << "\nAll tests passed!\n";
  return 0;
}