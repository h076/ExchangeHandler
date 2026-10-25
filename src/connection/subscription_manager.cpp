#include "subscription_manager.hpp"

subscription_manager::subscription_manager(formatter fmt)
  : format_fn(std::move(fmt)) {}

void subscription_manager::set_channels(const std::vector<channel_id>& channels)
{
  this->channels = channels;
}

std::vector<std::string> subscription_manager::get_subscription_messages() const
{
  if (!format_fn || channels.empty())
    return {};

  std::vector<std::string> messages;
  for (const auto& channel : channels)
  {
    messages.push_back(format_fn(channel));
  }

  return messages;
}
