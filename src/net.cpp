#include "quorum/net.hpp"

#include <cerrno>

namespace quorum::net {
namespace {

#ifdef _WIN32
constexpr bool kWindows = true;
#else
constexpr bool kWindows = false;
#endif

}  // namespace

Library::Library() {
#ifdef _WIN32
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
}

Library::~Library() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void Socket::close() {
    if (h_ == kInvalid) return;
#ifdef _WIN32
    ::closesocket(h_);
#else
    ::close(h_);
#endif
    h_ = kInvalid;
}

int last_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool last_error_was_would_block() {
#ifdef _WIN32
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR;
#endif
}

Socket listen_on(std::uint16_t port, std::string& error) {
    Socket sock(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!sock.valid()) {
        error = "socket() failed, code " + std::to_string(last_error());
        return {};
    }

    if constexpr (!kWindows) {
        int on = 1;
        ::setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on),
                     sizeof(on));
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(sock.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        error = "bind() to port " + std::to_string(port) + " failed, code " +
                std::to_string(last_error());
        return {};
    }
    if (::listen(sock.get(), 16) != 0) {
        error = "listen() failed, code " + std::to_string(last_error());
        return {};
    }
    return sock;
}

Socket accept_on(Handle listener) {
    sockaddr_in addr{};
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    return Socket(::accept(listener, reinterpret_cast<sockaddr*>(&addr), &len));
}

std::uint16_t local_port(Handle sock) {
    sockaddr_in addr{};
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    if (::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return 0;
    return ntohs(addr.sin_port);
}

bool set_nonblocking(Handle sock, bool on) {
#ifdef _WIN32
    u_long mode = on ? 1u : 0u;
    return ::ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = ::fcntl(sock, F_GETFL, 0);
    if (flags < 0) return false;
    flags = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return ::fcntl(sock, F_SETFL, flags) == 0;
#endif
}

long recv_some(Handle sock, char* buffer, std::size_t size) {
    return static_cast<long>(::recv(sock, buffer, static_cast<int>(size), 0));
}

long send_some(Handle sock, const char* buffer, std::size_t size) {
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    return static_cast<long>(::send(sock, buffer, static_cast<int>(size), flags));
}

bool send_all(Handle sock, std::string_view data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const long n = send_some(sock, data.data() + sent, data.size() - sent);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && last_error_was_would_block()) continue;
        return false;
    }
    return true;
}

}  // namespace quorum::net
