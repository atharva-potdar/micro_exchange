#pragma once
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>

class ServerSocket {
  int fd = -1;

 public:
  explicit ServerSocket(uint16_t port = 8080) {
    fd = socket(AF_INET, SOCK_STREAM, 0);  // socket() failed if < 0

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
        0) {  // setsockopt() failed
      close(fd);
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) <
        0) {  // bind() failed
      close(fd);
    }

    if (listen(fd, 128) < 0) {  // 128 - backlog queue length
      close(fd);
    }
  }
  ~ServerSocket() {
    if (fd >= 0) {
      close(fd);
    }
  }

  int accept() {
    int client_fd = ::accept(fd, nullptr, nullptr);  // accept() failed if < 0

    int nodelay = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) <
        0) {
      close(client_fd);
    }

    return client_fd;
  }
  static ssize_t recv(int fd, void* buf, size_t max) {
    return ::recv(fd, buf, max, 0);
  }
  static ssize_t send(int fd, const void* buf, size_t len) {
    return ::send(fd, buf, len, 0);
  }
};
