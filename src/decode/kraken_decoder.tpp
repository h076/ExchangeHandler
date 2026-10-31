// Template definitions for kraken_decoder: included at the bottom of
// kraken_decoder.hpp, not compiled on its own.
#pragma once

// Not needed for the build (the header includes this file); lets clangd parse
// the .tpp on its own. #pragma once on both files stops the cycle.
#include "kraken_decoder.hpp"

template <kraken_sink Sink>
kraken_decoder<Sink>::kraken_decoder(Sink& next)
  : m_next(next)
{
  m_snapshot.bids.reserve(1000);
  m_snapshot.asks.reserve(1000);
  m_update.bids.reserve(1000);
  m_update.asks.reserve(1000);
}

// Convert payload into a simdjson docuemnt object
template <kraken_sink Sink>
decode_status kraken_decoder<Sink>::do_decode(const std::string& payload)
{
  simdjson::ondemand::document doc;
  if (m_parser.iterate(payload.data(), payload.size(), payload.capacity()).get(doc))
  {
    return decode_status::malformed;
  } 

  std::string channel;
  if (doc["channel"].get_string().get(channel))
  {
    return decode_status::malformed;
  }

  if (channel == "book")
  {
    return decode_book(doc);
  }
  else if (channel == "heartbeat")
  {
    return decode_status::ignored;
  }
  else
  {
    return decode_status::unknown_type;
  }
}

template <kraken_sink Sink>
decode_status kraken_decoder<Sink>::decode_book(simdjson::ondemand::document& doc)
{
  std::string_view type;
  if (doc["type"].get_string().get(type))
  {
    return decode_status::malformed;
  }
  
  const bool is_snapshot = (type == "snapshot");
  if (!is_snapshot && type != "update")
  {
    return decode_status::unknown_type;
  }

  simdjson::ondemand::array data;
  if (doc["data"].get_array().get(data))
  {
    return decode_status::malformed;
  }

  // One entry per symbol. Each is parsed into the reused member struct and
  // handed to the sink by const ref, so the sink must copy anything it keeps.
  // On a parse error the remaining entries are dropped (earlier ones were
  // already delivered).
  std::size_t entries = 0;
  for (auto entry : data)
  {
    simdjson::ondemand::object obj;
    if (entry.get_object().get(obj))
    {
      return decode_status::malformed;
    }

    if (is_snapshot)
    {
      if (!kraken::parse::book_entry(obj, m_snapshot))
      {
        return decode_status::malformed;
      }
      m_next.on_book_snapshot(m_snapshot);
    }
    else
    {
      if (!kraken::parse::book_entry(obj, m_update))
      {
        return decode_status::malformed;
      }
      m_next.on_book_update(m_update);
    }
    ++entries;
  }

  return entries > 0 ? decode_status::ok : decode_status::malformed;
}
