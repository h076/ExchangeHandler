#include "ws_session.hpp"
#include <iostream>

ws_session::ws_session(net::any_io_executor ex, connection_config cc)
  : ws(ex, cc.tls), config(std::move(cc)) {}

void ws_session::connect()
{
  if (!message_handler || !state_handler)
  {
    std::cerr << "Both handlers must be set before connecting ws_session" << std::endl;
    return;
  }

  // co spawn the run function
  // use the state handler to handle errors
  net::co_spawn(ws.get_executor(), run(),
      make_state_handler(connection_status::error));
}

auto ws_session::run() -> net::awaitable<void>
{
  auto& tcp = get_lowest_layer(ws);

  // Resolve
  net::ip::tcp::resolver resolver(ws.get_executor());
  net::ip::tcp::resolver::results_type results
      = co_await resolver.async_resolve(config.host, "wss", net::use_awaitable);

  // Connect TCP
  tcp.expires_after(std::chrono::seconds(15));
  net::ip::tcp::endpoint ep
      = co_await tcp.async_connect(results, net::use_awaitable);

  // TLS handshake
  if (!SSL_set_tlsext_host_name(
          ws.next_layer().native_handle(), config.host.c_str())) {
    throw beast::system_error(
        beast::error_code(
            static_cast<int>(::ERR_get_error()),
            net::error::get_ssl_category()));
  }

  tcp.expires_after(std::chrono::seconds(15));
  co_await ws.next_layer().async_handshake(
      net::ssl::stream_base::client, net::use_awaitable);

  // WebSocket Upgrade
  tcp.expires_after(std::chrono::seconds(15));
  co_await ws.async_handshake(
      config.host + ":" + std::to_string(ep.port()),
      config.target,
      net::use_awaitable);

  if (state_handler)
  {
    state_handler(connection_status::connected);
  }

  // Read Loop via separate coroutine
  co_await read_loop(ws);
}

auto ws_session::read_loop(auto& ws_stream) -> net::awaitable<void>
{
  boost::beast::flat_buffer buffer;
  for (;;) {
    auto bytes_read = co_await ws_stream.async_read(buffer, net::use_awaitable);

    if (message_handler) {
      auto buf = buffer.data();
      message_handler(std::span<const std::byte>(
              static_cast<const std::byte*>(
                buf.data()), bytes_read));
    }

    buffer.consume(bytes_read);
  }
}

void ws_session::disconnect()
{
  net::co_spawn(ws.get_executor(), teardown(), 
      make_state_handler(connection_status::error));
}

auto ws_session::teardown() -> net::awaitable<void>
{
  // Can just push out async_close and beast will terminate an inflight read
  // with wbsocket::error::closed
  co_await ws.async_close(websocket::close_code::normal, net::use_awaitable);

  if (state_handler)
  {
    state_handler(connection_status::disconnected);
  }
}

void ws_session::send(std::string_view payload)
{
  net::co_spawn(ws.get_executor(), write(payload), 
      make_state_handler(connection_status::error));
}

auto ws_session::write(std::string_view& payload) -> net::awaitable<void>
{
  std::size_t bytes_sent = co_await ws.async_write(net::buffer(payload), net::use_awaitable);
  if (bytes_sent < payload.size())
  {
    throw std::runtime_error("Partial write");
  }
}

std::function<void(std::exception_ptr)> ws_session::make_state_handler(connection_status status)
{
  return [self = shared_from_this(), status](std::exception_ptr e) 
    {
      if (e) 
      {
        try 
        {
          std::rethrow_exception(e);
        } 
        catch (std::exception& ex) 
        {
          std::cerr << "ws_session error: " << ex.what() << std::endl;
          if (self->state_handler) 
          {
            self->state_handler(status);
          }
        }
      }
    };
}

void ws_session::set_message_handler(std::function<void(std::span<const std::byte>)> handler)
{
  message_handler = std::move(handler);
}

void ws_session::set_state_handler(std::function<void(connection_status)> handler)
{
  state_handler = std::move(handler);
}
