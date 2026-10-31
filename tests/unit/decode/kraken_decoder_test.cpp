#include <iostream>
#include <string>
#include <vector>

#include "kraken_decoder.hpp"
#include "../check.hpp"

// Records what the decoder forwards. The decoder reuses its structs, so the
// sink copies them (as a real sink must for anything it keeps).
struct recording_sink
{
  int snapshots = 0;
  int updates = 0;
  std::vector<kraken::book_snapshot> snapshot_log;
  std::vector<kraken::book_update> update_log;

  void on_book_snapshot(const kraken::book_snapshot& s) { ++snapshots; snapshot_log.push_back(s); }
  void on_book_update(const kraken::book_update& u) { ++updates; update_log.push_back(u); }
};

static bool same(const std::vector<kraken::level>& a, const std::vector<kraken::level>& b)
{
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
  {
    if (a[i].price != b[i].price || a[i].qty != b[i].qty) return false;
  }
  return true;
}

// Kraken's documented examples
static const char* snapshot_json = R"({
  "channel": "book",
  "type": "snapshot",
  "data": [
    {
      "symbol": "BTC/USD",
      "bids": [ { "price": 65000.0, "qty": 1.5 }, { "price": 64999.5, "qty": 3.0 } ],
      "asks": [ { "price": 65001.0, "qty": 0.8 }, { "price": 65002.0, "qty": 2.1 } ],
      "checksum": 2439117997,
      "timestamp": "2023-10-06T17:35:55.440295Z"
    }
  ]
})";

static const char* update_json = R"({
  "channel": "book",
  "type": "update",
  "data": [
    {
      "symbol": "BTC/USD",
      "bids": [ { "price": 64999.5, "qty": 2.0 }, { "price": 64998.0, "qty": 0 } ],
      "asks": [],
      "checksum": 2114181697,
      "timestamp": "2023-10-06T17:35:55.440295Z"
    }
  ]
})";

static_assert(kraken_sink<recording_sink>);

// Built like the connection layer builds received_message::payload: with
// spare capacity for simdjson's padding requirement
static std::string padded(std::string_view json)
{
  std::string s;
  s.reserve(json.size() + simdjson::SIMDJSON_PADDING);
  s.assign(json);
  return s;
}

// ---------------------------------------------------------------------------

static void heartbeat_is_ignored()
{
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  CHECK(d.decode(padded(R"({"channel":"heartbeat"})")) == decode_status::ignored);
}

static void unknown_channel_is_reported()
{
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  CHECK(d.decode(padded(R"({"channel":"status","type":"update","data":[]})")) == decode_status::unknown_type);
}

static void invalid_json_is_malformed()
{
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  CHECK(d.decode(padded(R"({"channel":)")) == decode_status::malformed);
  CHECK(d.decode(padded("not json")) == decode_status::malformed);
}

static void missing_channel_is_malformed()
{
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  CHECK(d.decode(padded(R"({"method":"subscribe","success":true})")) == decode_status::malformed);
}

static void unpadded_payload_is_rejected()
{
  // Documents simdjson's requirement: without spare capacity the parser refuses
  // the input, which is why the connection layer reserves payload_padding
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  std::string tight;
  tight.assign(R"({"channel":"heartbeat"})");
  tight.shrink_to_fit();
  if (tight.capacity() - tight.size() < simdjson::SIMDJSON_PADDING)
  {
    CHECK(d.decode(tight) == decode_status::malformed);
  }
}

static void stats_count_each_status()
{
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  d.decode(padded(R"({"channel":"heartbeat"})"));
  d.decode(padded(R"({"channel":"heartbeat"})"));
  d.decode(padded(R"({"channel":"status"})"));
  d.decode(padded("garbage"));

  CHECK(d.stats().ignored == 2);
  CHECK(d.stats().unknown_type == 1);
  CHECK(d.stats().malformed == 1);
  CHECK(d.stats().ok == 0);
}

static void snapshot_is_decoded()
{
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  CHECK(d.decode(padded(snapshot_json)) == decode_status::ok);

  CHECK(sink.snapshots == 1);
  CHECK(sink.updates == 0);
  if (sink.snapshot_log.size() != 1) return;
  const auto& s = sink.snapshot_log[0];

  CHECK(s.sym.view() == "BTC/USD");
  CHECK(s.checksum == 2439117997u);
  CHECK(same(s.bids, {{6500000000000, 150000000}, {6499950000000, 300000000}}));
  CHECK(same(s.asks, {{6500100000000, 80000000}, {6500200000000, 210000000}}));
}

static void update_is_decoded()
{
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  CHECK(d.decode(padded(update_json)) == decode_status::ok);

  CHECK(sink.updates == 1);
  CHECK(sink.snapshots == 0);
  if (sink.update_log.size() != 1) return;
  const auto& u = sink.update_log[0];

  CHECK(u.sym.view() == "BTC/USD");
  CHECK(u.checksum == 2114181697u);
  CHECK(u.timestamp_ns == 1696613755440295000);
  CHECK(same(u.bids, {{6499950000000, 200000000}, {6499800000000, 0}}));   // qty 0 = remove
  CHECK(u.asks.empty());
}

static void caller_sink_receives_messages()
{
  // The decoder must feed the sink it was given, not a copy of it
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  d.decode(padded(snapshot_json));
  d.decode(padded(update_json));
  d.decode(padded(update_json));
  CHECK(sink.snapshots == 1);
  CHECK(sink.updates == 2);
  CHECK(d.stats().ok == 3);
}

static void reused_buffers_are_cleared()
{
  // A smaller second message must not keep levels from the first
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  d.decode(padded(snapshot_json));
  d.decode(padded(R"({"channel":"book","type":"snapshot","data":[{"symbol":"ETH/USD",
      "bids":[{"price":3000.25,"qty":1}],"asks":[],"checksum":1}]})"));

  if (sink.snapshot_log.size() != 2) { CHECK(sink.snapshot_log.size() == 2); return; }
  const auto& s = sink.snapshot_log[1];
  CHECK(s.sym.view() == "ETH/USD");
  CHECK(same(s.bids, {{300025000000, 100000000}}));
  CHECK(s.asks.empty());
}

static void multiple_entries_each_delivered()
{
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  CHECK(d.decode(padded(R"({"channel":"book","type":"update","data":[
      {"symbol":"BTC/USD","bids":[{"price":1,"qty":1}],"asks":[],"checksum":1,"timestamp":"2023-10-06T17:35:55Z"},
      {"symbol":"ETH/USD","bids":[],"asks":[{"price":2,"qty":2}],"checksum":2,"timestamp":"2023-10-06T17:35:56Z"}
  ]})")) == decode_status::ok);

  CHECK(sink.updates == 2);
  if (sink.update_log.size() != 2) return;
  CHECK(sink.update_log[0].sym.view() == "BTC/USD");
  CHECK(sink.update_log[1].sym.view() == "ETH/USD");
  CHECK(sink.update_log[1].timestamp_ns - sink.update_log[0].timestamp_ns == 1'000'000'000);
}

static void missing_side_is_empty()
{
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  CHECK(d.decode(padded(R"({"channel":"book","type":"update","data":[
      {"symbol":"BTC/USD","asks":[{"price":5,"qty":1}],"checksum":7,"timestamp":"2023-10-06T17:35:55Z"}]})"))
        == decode_status::ok);
  if (sink.update_log.size() != 1) return;
  CHECK(sink.update_log[0].bids.empty());
  CHECK(sink.update_log[0].asks.size() == 1);
}

static void string_prices_are_accepted()
{
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  CHECK(d.decode(padded(R"({"channel":"book","type":"snapshot","data":[
      {"symbol":"BTC/USD","bids":[{"price":"65000.1","qty":"0.5"}],"asks":[],"checksum":1}]})"))
        == decode_status::ok);
  if (sink.snapshot_log.size() != 1) return;
  CHECK(same(sink.snapshot_log[0].bids, {{6500010000000, 50000000}}));
}

static void unknown_book_type_is_reported()
{
  recording_sink sink;
  kraken_decoder<recording_sink> d(sink);
  CHECK(d.decode(padded(R"({"channel":"book","type":"delta","data":[]})")) == decode_status::unknown_type);
}

static void bad_book_messages_are_malformed()
{
  const char* bad[] = {
    // no type / no data / empty data / data not an array
    R"({"channel":"book","data":[]})",
    R"({"channel":"book","type":"snapshot"})",
    R"({"channel":"book","type":"snapshot","data":[]})",
    R"({"channel":"book","type":"snapshot","data":{}})",
    // bad fields
    R"({"channel":"book","type":"snapshot","data":[{"bids":[],"asks":[],"checksum":1}]})",
    R"({"channel":"book","type":"snapshot","data":[{"symbol":"","bids":[],"asks":[],"checksum":1}]})",
    R"({"channel":"book","type":"snapshot","data":[{"symbol":"THIS/IS/WAY/TOO/LONG","bids":[],"asks":[],"checksum":1}]})",
    R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD","bids":[{"price":"abc","qty":1}],"asks":[],"checksum":1}]})",
    R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD","bids":[{"qty":1}],"asks":[],"checksum":1}]})",
    R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD","bids":[{"price":1.123456789,"qty":1}],"asks":[],"checksum":1}]})",
    R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD","bids":[],"asks":[]}]})",
    R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD","bids":[],"asks":[],"checksum":4294967296}]})",
    R"({"channel":"book","type":"snapshot","data":[{"symbol":"BTC/USD","bids":[],"asks":[],"checksum":-1}]})",
    // update needs a valid timestamp
    R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD","bids":[],"asks":[],"checksum":1}]})",
    R"({"channel":"book","type":"update","data":[{"symbol":"BTC/USD","bids":[],"asks":[],"checksum":1,"timestamp":"yesterday"}]})",
  };

  for (const char* json : bad)
  {
    recording_sink sink;
    kraken_decoder<recording_sink> d(sink);
    const auto status = d.decode(padded(json));
    CHECK(status == decode_status::malformed);
    CHECK(sink.snapshots + sink.updates == 0);
    if (status != decode_status::malformed) std::cout << "\n    input: " << json;
  }
}

int main()
{
  std::cout << "Running kraken_decoder tests...\n\n";

  test::run("HeartbeatIsIgnored", heartbeat_is_ignored);
  test::run("UnknownChannelIsReported", unknown_channel_is_reported);
  test::run("InvalidJsonIsMalformed", invalid_json_is_malformed);
  test::run("MissingChannelIsMalformed", missing_channel_is_malformed);
  test::run("UnpaddedPayloadIsRejected", unpadded_payload_is_rejected);
  test::run("StatsCountEachStatus", stats_count_each_status);
  test::run("SnapshotIsDecoded", snapshot_is_decoded);
  test::run("UpdateIsDecoded", update_is_decoded);
  test::run("CallerSinkReceivesMessages", caller_sink_receives_messages);
  test::run("ReusedBuffersAreCleared", reused_buffers_are_cleared);
  test::run("MultipleEntriesEachDelivered", multiple_entries_each_delivered);
  test::run("MissingSideIsEmpty", missing_side_is_empty);
  test::run("StringPricesAreAccepted", string_prices_are_accepted);
  test::run("UnknownBookTypeIsReported", unknown_book_type_is_reported);
  test::run("BadBookMessagesAreMalformed", bad_book_messages_are_malformed);

  return test::summary();
}
