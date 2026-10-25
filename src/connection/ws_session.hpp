#pragma once

#include <boost/beast.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include "ws_session_interface.hpp"

namespace net = boost::asio;
namespace beast = boost::beast;
using namespace boost::beast;
using namespace boost::beast::websocket;
  
struct connection_config
{
  std::string host;
  uint16_t port;
  std::string target;
  net::ssl::context tls;
};

// The stream will not own the io_context, instead we will pass it a strand
class ws_session : public ws_session_interface, public std::enable_shared_from_this<ws_session>
{
  public:
    
    ws_session(net::any_io_executor ex, connection_config cc);

    // Make the session non-copyable and non-movable
    ws_session(const ws_session& p) = delete;
    ws_session &operator=(const ws_session& p) = delete;
    ws_session(ws_session&& p) = delete;
    ws_session &operator=(ws_session&& p) = delete;

    void connect() override;
    void disconnect() override;
    void send(std::string payload) override;

    // handles application data
    void set_message_handler(std::function<void(std::span<const std::byte>)> handler) override;

    // handles an event that is independant of any application payload
    void set_state_handler(std::function<void(connection_status)> handler) override;

  private:
  
    net::awaitable<void> run();

    net::awaitable<void> read_loop();
    net::awaitable<void> write_loop();

    std::function<void(std::exception_ptr)> make_run_handler();
    std::function<void(std::exception_ptr)> make_failure_handler(const char* op);

  private:
    
    stream<net::ssl::stream<tcp_stream>> ws;
    connection_config config;

    // Using a boost channel to handle the outbound queue on a connection object
    using write_channel = net::experimental::channel<void(boost::system::error_code, std::string)>;
    static constexpr std::size_t write_queue_capacity = 256;
    write_channel write_ch;

    std::function<void(std::span<const std::byte>)> message_handler;
    std::function<void(connection_status)> state_handler;

    bool running;
    bool stopping;
    
};
