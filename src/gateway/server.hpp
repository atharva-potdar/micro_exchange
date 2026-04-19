#pragma once
#include <unistd.h>

#include <cstddef>

#include "framer.hpp"
#include "orderbook/orderbook.hpp"
#include "protocol.hpp"
#include "socket.hpp"

template <size_t MAX_ORDERS, size_t MAX_LEVELS>
class Server {
 public:
  using Book = OrderBook<MAX_ORDERS, MAX_LEVELS>;

 private:
  ServerSocket socket_;
  Book& book_;

  // Scratch buffer for raw recv bytes to hold the largest wire message
  static constexpr size_t kRecvBuf = 4096;
  std::byte recv_buf_[kRecvBuf]{};

  // Response scratch buffer.
  static constexpr size_t kSendBuf = 4096;
  std::byte send_buf_[kSendBuf]{};

  // Sends exactly `len` bytes, retrying on short writes.
  // Returns false if the connection is lost.
  [[nodiscard]] auto send_all(int fd, size_t len) -> bool {
    size_t sent = 0;
    while (sent < len) {
      ssize_t n = ServerSocket::send(fd, send_buf_ + sent, len - sent);
      if (n <= 0) { return false;
}
      sent += static_cast<size_t>(n);
    }
    return true;
  }

  auto handle_order_add(int client_fd, const WireOrderAdd& msg) -> bool {
    Side side = (msg.side == 0) ? Side::Buy : Side::Sell;

    // Attempt to execute (match then rest).
    std::expected<void, Error> result;
    if (side == Side::Buy) {
      result =
          book_.template execute_order<Side::Buy>(msg.id, msg.price, msg.qty);
    } else {
      result =
          book_.template execute_order<Side::Sell>(msg.id, msg.price, msg.qty);
}

    // Drain and send any trades that were generated.
    Trade trades[512];
    size_t n_trades = book_.drain_trades(trades, std::size(trades));
    for (size_t i = 0; i < n_trades; ++i) {
      WireTrade wt{};
      wt.hdr.type = static_cast<uint8_t>(MessageType::Trade);
      wt.hdr.length = static_cast<uint16_t>(sizeof(WireTrade));
      wt.buyer = trades[i].buyer_order_id;
      wt.seller = trades[i].seller_order_id;
      wt.price = trades[i].price;
      wt.qty = trades[i].quantity;
      size_t len = serialize(wt, send_buf_);
      if (!send_all(client_fd, len)) { return false;
}
    }

    // Send ack.
    WireAck ack{};
    ack.hdr.type = static_cast<uint8_t>(MessageType::Ack);
    ack.hdr.length = static_cast<uint16_t>(sizeof(WireAck));
    ack.id = msg.id;
    ack.status = result ? 0 : 1;
    size_t len = serialize(ack, send_buf_);
    return send_all(client_fd, len);
  }

  auto handle_order_cancel(int client_fd, const WireOrderCancel& msg) -> bool {
    auto result = book_.cancel_order(msg.id);

    WireAck ack{};
    ack.hdr.type = static_cast<uint8_t>(MessageType::Ack);
    ack.hdr.length = static_cast<uint16_t>(sizeof(WireAck));
    ack.id = msg.id;
    ack.status = result ? 0 : 1;
    size_t len = serialize(ack, send_buf_);
    return send_all(client_fd, len);
  }

  // Per-client read loop. Returns when the client disconnects or a send fails.
  void serve_client(int client_fd) {
    Framer framer;

    // A handler that dispatches parsed messages and writes responses.
    bool ok = true;
    auto dispatch = [&](auto& msg) -> auto {
      using T = std::decay_t<decltype(msg)>;
      if constexpr (std::is_same_v<T, WireOrderAdd>) {
        ok = handle_order_add(client_fd, msg);
      } else if constexpr (std::is_same_v<T, WireOrderCancel>) {
        ok = handle_order_cancel(client_fd, msg);
      }
    };

    while (ok) {
      ssize_t n = ServerSocket::recv(client_fd, recv_buf_, kRecvBuf);
      if (n <= 0) { break;  // 0 = clean disconnect, <0 = error
}

      framer.ingest(recv_buf_, static_cast<size_t>(n));

      while (ok && framer.try_parse_one(dispatch)) {
        // dispatch sets ok = false on a send error; the inner while exits,
        // then the outer while exits too.
      }
    }
  }

 public:
  explicit Server(Book& book, uint16_t port = 8080)
      : socket_(port), book_(book) {}

  // Blocking accept loop. Handles one client at a time.
  void run() {
    for (;;) {
      int client_fd = socket_.accept();
      if (client_fd < 0) { [[unlikely]]
        continue;
}
      serve_client(client_fd);
      ::close(client_fd);
    }
  }
};
