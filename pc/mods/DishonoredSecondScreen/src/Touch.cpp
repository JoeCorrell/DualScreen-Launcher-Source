#include <winsock2.h>
#include <ws2tcpip.h>
#include "Touch.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace dishonored {
bool ParseTouch(std::string_view packet, Touch& event) noexcept {
    if (packet.empty() || packet.size() >= 96 || packet.find('\0') != std::string_view::npos) return false;
    if (packet == "cancel") { event = {Gesture::Cancel}; return true; }
    if (packet == "page next") { event = {Gesture::NextPage}; return true; }
    if (packet == "page previous") { event = {Gesture::PreviousPage}; return true; }
    char text[96]{}, verb[16]{};
    std::memcpy(text, packet.data(), packet.size());
    float x{}, y{}, factor{1}; int consumed{};
    if (std::sscanf(text, "%15s %f %f %n", verb, &x, &y, &consumed) != 3 || !consumed ||
        !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1) return false;
    Gesture gesture{};
    if (!std::strcmp(verb, "down")) gesture = Gesture::Down;
    else if (!std::strcmp(verb, "move")) gesture = Gesture::Move;
    else if (!std::strcmp(verb, "up")) gesture = Gesture::Up;
    else if (!std::strcmp(verb, "tap") || !std::strcmp(verb, "select")) gesture = Gesture::Tap;
    else if (!std::strcmp(verb, "hold")) gesture = Gesture::Hold;
    else if (!std::strcmp(verb, "pinch")) gesture = Gesture::Pinch;
    else return false;
    // Stock WEMU includes contact duration on both tap and hold packets.
    // Duration is metadata, not a pinch factor, and never changes the action.
    if (gesture == Gesture::Pinch || ((gesture == Gesture::Tap || gesture == Gesture::Hold) && std::size_t(consumed) < packet.size())) {
        int end{};
        if (std::sscanf(text + consumed, "%f %n", &factor, &end) != 1 || !end || !std::isfinite(factor)) return false;
        consumed += end;
        if (gesture == Gesture::Pinch && (factor < 0.1f || factor > 10.0f)) return false;
        if (gesture != Gesture::Pinch && factor < 0) return false;
    }
    if (std::size_t(consumed) != packet.size()) return false;
    event = {gesture, x, y, factor}; return true;
}
TouchSocket::~TouchSocket() { Close(); }
bool TouchSocket::Open() noexcept {
    if (socket_ != uintptr_t(INVALID_SOCKET)) return true;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) return false;
    winsock_ = true;
    // Transfer ownership directly to the member closed by Close/destruction;
    // the local channel below is only a borrowed Winsock-typed handle.
    socket_ = uintptr_t(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (socket_ == uintptr_t(INVALID_SOCKET)) { Close(); return false; }
    const SOCKET channel = SOCKET(socket_);
    BOOL exclusive = TRUE;
    if (setsockopt(channel, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive))) {
        Close(); return false;
    }
    sockaddr_in address{}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(7788);
    u_long nonblocking = 1;
    if (bind(channel, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
        ioctlsocket(channel, FIONBIO, &nonblocking)) { Close(); return false; }
    return true;
}
void TouchSocket::Close() noexcept {
    if (socket_ != uintptr_t(INVALID_SOCKET)) closesocket(SOCKET(socket_));
    socket_ = uintptr_t(INVALID_SOCKET);
    if (winsock_) WSACleanup();
    winsock_ = false;
}
bool TouchSocket::Receive(Touch& event) noexcept {
    if (socket_ == uintptr_t(INVALID_SOCKET)) return false;
    char packet[96]{}; sockaddr_in sender{}; int size = sizeof(sender);
    int count = recvfrom(SOCKET(socket_), packet, sizeof(packet), 0, reinterpret_cast<sockaddr*>(&sender), &size);
    if (count == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) ++rejected_;
        return false;
    }
    if (sender.sin_family != AF_INET || sender.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
        !ParseTouch(std::string_view(packet, std::size_t(count)), event)) {
        ++rejected_; return false;
    }
    return true;
}
} // namespace dishonored
