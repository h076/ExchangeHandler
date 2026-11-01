#pragma once

#include "decoder.hpp"
#include "../common/kraken_messages.hpp"
#include "kraken_parse.hpp"

#include <simdjson.h>

template <typename S>
concept kraken_sink 
  = requires(S s, const kraken::book_snapshot& snap, const kraken::book_update& upd)
{
  s.on_book_snapshot(snap);
  s.on_book_update(upd);
};          

template <kraken_sink Sink>
class kraken_decoder final : public decoder
{
  public:
    
    explicit kraken_decoder(Sink& next);

  protected:

    decode_status do_decode(const received_message& msg) override;

  private:

    decode_status decode_book(simdjson::ondemand::document& doc, const receive_info& rx);

    Sink& m_next;   // reference: the decoder feeds the caller's sink, not a copy
    simdjson::ondemand::parser m_parser;
    kraken::book_snapshot m_snapshot;
    kraken::book_update m_update;

};

// Template definitions: must be visible wherever kraken_decoder<Sink> is used
#include "kraken_decoder.tpp"
