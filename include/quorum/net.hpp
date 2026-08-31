// Portable wrapper over Winsock and BSD sockets. The rest of the project never
// sees a platform socket call.
#pragma once

#ifdef _WIN32
#ifndef FD_SETSIZE
#define FD_SETSIZE 64
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace quorum::net {

#ifdef _WIN32
using Handle = SOCKET;
inline const Handle kInvalid = INVALID_SOCKET;
#else
using Handle = int;
inline constexpr Handle kInvalid = -1;
#endif

class Library {
public:
    Library();
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
};

class Socket {
public:
    Socket() = default;
    explicit Socket(Handle h) : h_(h) {}
    ~Socket() { close(); }

    Socket(Socket&& other) noexcept : h_(other.h_) { other.h_ = kInvalid; }
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            h_ = other.h_;
            other.h_ = kInvalid;
        }
        return *this;
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Handle get() const { return h_; }
    bool valid() const { return h_ != kInvalid; }
    void close();

private:
    Handle h_ = kInvalid;
};

Socket listen_on(std::uint16_t port, std::string& error);
Socket accept_on(Handle listener);
std::uint16_t local_port(Handle sock);
bool set_nonblocking(Handle sock, bool on);
long recv_some(Handle sock, char* buffer, std::size_t size);
long send_some(Handle sock, const char* buffer, std::size_t size);
bool send_all(Handle sock, std::string_view data);
bool last_error_was_would_block();
int last_error();

}  // namespace quorum::net
