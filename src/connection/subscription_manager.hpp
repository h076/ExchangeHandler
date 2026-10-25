#pragma once
#include <string>
#include <vector>
#include <functional>

struct channel_id
{
  std::string id;
};

class subscription_manager
{
  public:
    using formatter = std::function<std::string(const channel_id&)>;

    subscription_manager(formatter fmt);

    void set_channels(const std::vector<channel_id>& channels);
    std::vector<std::string> get_subscription_messages() const;

  private:
    formatter format_fn;
    std::vector<channel_id> channels;
};
