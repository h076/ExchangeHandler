#include "ws_session.hpp"
#include <iostream>

ws_session::ws_session(net::any_io_executor ex, connection_config cc)
  : ws(ex, cc.tls), config(std::move(cc)), write_ch(ex, write_queue_capacity), running(false), stopping(false) {}

void ws_session::connect()
{
  if (!message_handler || !state_handler)
  {
    std::cerr << "Both handlers must be set before connecting ws_session" << std::endl;
    return;
  }

  if (running)
  {
    return;
  }

  running = true;
  stopping = false;

  write_ch.reset();

  // co spawn the run function
  // use the state handler to handle errors
  net::co_spawn(ws.get_executor(), run(), make_run_handler());
}

auto ws_session::run() -> net::awaitable<void>
{
  auto& tcp = get_lowest_layer(ws);

  // Resolve
  net::ip::tcp::resolver resolver(ws.get_executor());
  net::ip::tcp::resolver::results_type results
      = co_await resolver.async_resolve(config.host, std::to_string(config.port), net::use_awaitable);

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

  // Cancels previous 15 second timeout
  tcp.expires_never();

  // Turn on ping/pongs and keep-alives
  auto opt = stream_base::timeout::suggested(role_type::client);
  opt.idle_timeout = std::chrono::seconds(15);
  opt.keep_alive_pings = true;
  ws.set_option(opt);

  if (state_handler)
  {
    state_handler(connection_status::connected);
  }

  // Read and Write loop 
  using namespace net::experimental::awaitable_operators;
  co_await (write_loop() && read_loop());
}

auto ws_session::read_loop() -> net::awaitable<void>
{
  boost::beast::flat_buffer buffer;
  for (;;) {
    auto bytes_read = co_await ws.async_read(buffer, net::use_awaitable);

    if (message_handler) {
      auto buf = buffer.data();
      message_handler(std::span<const std::byte>(
              static_cast<const std::byte*>(
                buf.data()), bytes_read));
    }

    buffer.consume(bytes_read);
  }
}

auto ws_session::write_loop() -> net::awaitable<void>
{
  for (;;)
  {
    auto [ec, msg] = co_await write_ch.async_receive(net::as_tuple(net::use_awaitable));

    if (ec == net::experimental::channel_errc::channel_closed)
    {
      // disconnect() closed the channel
      // queue has been drained
      co_await ws.async_close(websocket::close_code::normal, net::use_awaitable);
      co_return;
    }
    else if (ec)
    {
      throw boost::system::system_error(ec);
    }

    co_await ws.async_write(net::buffer(msg), net::use_awaitable);
  }
}

void ws_session::disconnect()
{
  // nothing in flight, report directly to state manager
  if (!running)
  {
    if (state_handler)
    {
      state_handler(connection_status::disconnected);
    } 
    return;
  }

  if (stopping)
  {
    return;
  }

  // Mark the close as intentional before read loop is woken up
  stopping = true;
  write_ch.close();
}

void ws_session::send(std::string payload)
{
  // dispatch onto the strand, the channel and flags are not thread safe
  net::dispatch(ws.get_executor(),
      [self = shared_from_this(), p = std::move(payload)] () mutable
      {
        if (!self->running || self->stopping)
        {
          std::cerr << "ws_session: not connected, dropping message" << std::endl;
          return;
        }

        // never blocks, false == full queue
        // but we would rarely send out that many messages so kill socket on this
        if (!self->write_ch.try_send(boost::system::error_code{}, std::move(p)))
        {
          std::cerr << "ws_session: write queue is full, dropping message" << std::endl;
          beast::error_code ignore;
          beast::get_lowest_layer(self->ws).socket().close(ignore);
        }
      });
}

inline std::string describe(std::exception_ptr e)
{
  try 
  { 
    std::rethrow_exception(e); 
  }
  catch (const net::multiple_exceptions& ex)
  {
    return describe(ex.first_exception());
  }
  catch (const boost::system::system_error& ex) 
  { 
    return ex.code().message(); 
  }
  catch (const std::exception& ex)              
  { 
    return ex.what(); 
  }
  catch (...)                                   
  { 
    return "unknown exception"; 
  }
}
 
// Completion of run(): the session is over, report exactly one status.
std::function<void(std::exception_ptr)> ws_session::make_run_handler()
{
  return [self = shared_from_this()](std::exception_ptr e)
  {
    self->running = false;

    connection_status status = connection_status::disconnected;
    if (!self->stopping)
    {
      status = connection_status::error;
      if (e)
      {
        std::cerr << "ws_session lost: " << describe(e)
                  << " (close reason: '" << self->ws.reason().reason << "')" << std::endl;
      }
    }
    
    self->stopping = false;

    if (self->state_handler)
    {
      self->state_handler(status);
    }
  };
}

// Completion of a side operation (write / close). Never reports state itself;
// on failure it kills the socket so the read loop ends and run()'s handler reports once.
std::function<void(std::exception_ptr)> ws_session::make_failure_handler(const char* op)
{
  return [self = shared_from_this(), op](std::exception_ptr e)
  {
    if (!e)
    {
      return;
    }

    std::cerr << "ws_session " << op << " failed: " << describe(e) << std::endl;

    beast::error_code ignored;
    beast::get_lowest_layer(self->ws).socket().close(ignored);
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
