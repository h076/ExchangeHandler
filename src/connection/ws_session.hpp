#include <boost/beast.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

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
class ws_session : public ws_session_interface, std::enable_shared_from_this<ws_session>
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
    void send(std::string_view payload) override;

    // handles application data
    void set_message_handler(std::function<void(std::span<const std::byte>)> handler) override;

    // handles an event that is independant of any application payload
    void set_state_handler(std::function<void(connection_status)> handler) override;

  private:
    
    net::awaitable<void> run();

    template <typename StreamType>
    net::awaitable<void> read_loop(StreamType& ws_stream);
    net::awaitable<void> write(std::string_view& payload);
    net::awaitable<void> teardown();

    std::function<void(std::exception_ptr)> make_state_handler(connection_status status);

  private:
    
    stream<net::ssl::stream<tcp_stream>> ws;
    connection_config config;

    std::function<void(std::span<const std::byte>)> message_handler;
    std::function<void(connection_status)> state_handler;
    
};
