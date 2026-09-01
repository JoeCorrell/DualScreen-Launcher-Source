#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "GameActions.h"
#include "Log.h"
#include "TouchInput.h"

namespace dxs {
namespace {

// 7787 belongs to WEMU's second-screen discovery beacon. Use a dedicated
// loopback port for ordered native guest gestures.
constexpr unsigned short kTouchPort = 7788;
std::atomic<bool> g_running{false};
std::atomic<uint32_t> g_received{0};
std::atomic<uint32_t> g_rejected{0};
std::atomic<SOCKET> g_socket{INVALID_SOCKET};
HANDLE g_thread = nullptr;
SRWLOCK g_lifecycle_lock = SRWLOCK_INIT;
SRWLOCK g_feedback_lock = SRWLOCK_INIT;
sockaddr_in g_feedback_peer = {};
bool g_feedback_peer_valid = false;
bool g_winsock_started = false;

enum class TouchState {
  kStopped,
  kRunning,
  // A failed worker join retains the thread handle and Winsock reference. A
  // later Stop can finish cleanup, while Start must not create a second worker.
  kStopping,
};

TouchState g_state = TouchState::kStopped;

const char* GestureName(GuestGesture gesture) {
  switch (gesture) {
    case GuestGesture::kTap: return "tap";
    case GuestGesture::kDown: return "down";
    case GuestGesture::kMove: return "move";
    case GuestGesture::kUp: return "up";
    case GuestGesture::kCancel: return "cancel";
    case GuestGesture::kHold: return "hold";
    case GuestGesture::kPinch: return "pinch";
  }
  return "unknown";
}

bool IsCommandOnly(const char* packet, const char* command) {
  if (!packet || !command) return false;
  const size_t length = std::strlen(command);
  if (std::strncmp(packet, command, length) != 0) return false;
  const char* tail = packet + length;
  tail += std::strspn(tail, " \t\r\n");
  return *tail == '\0';
}

bool ParseArguments(const char* packet, const char* command, float* x, float* y,
                    float* value = nullptr) {
  if (!packet || !command || !x || !y) return false;
  const size_t command_length = std::strlen(command);
  if (std::strncmp(packet, command, command_length) != 0) return false;
  const char* arguments = packet + command_length;
  if (*arguments != ' ' && *arguments != '\t' &&
      *arguments != '\r' && *arguments != '\n') {
    return false;
  }
  arguments += std::strspn(arguments, " \t\r\n");
  if (*arguments == '\0') return false;

  int consumed = 0;
  const int fields = value
      ? std::sscanf(arguments, "%f %f %f %n", x, y, value, &consumed)
      : std::sscanf(arguments, "%f %f %n", x, y, &consumed);
  const int expected_fields = value ? 3 : 2;
  if (fields != expected_fields || consumed <= 0) return false;
  const char* tail = arguments + consumed;
  tail += std::strspn(tail, " \t\r\n");
  return *tail == '\0';
}

bool ValidFeedbackPeer(const sockaddr_in& sender, int sender_size) {
  if (sender_size < static_cast<int>(sizeof(sender)) ||
      sender.sin_family != AF_INET || sender.sin_port == 0) {
    return false;
  }
  // The socket is bound to INADDR_LOOPBACK. Keep the reply target equally
  // strict in case a future socket option or Wine implementation broadens it.
  return (ntohl(sender.sin_addr.s_addr) & 0xFF000000u) == 0x7F000000u;
}

void PublishFeedbackPeer(const sockaddr_in& sender) {
  AcquireSRWLockExclusive(&g_feedback_lock);
  // Stop publishes running=false before taking this lock. Recheck here so a
  // receiver which queued its final packet just before shutdown cannot restore
  // a peer after Stop invalidated the socket and feedback state.
  if (g_running.load(std::memory_order_acquire) &&
      g_socket.load(std::memory_order_acquire) != INVALID_SOCKET) {
    g_feedback_peer = sender;
    g_feedback_peer_valid = true;
  }
  ReleaseSRWLockExclusive(&g_feedback_lock);
}

bool QueueTouch(GuestGesture gesture, float x, float y, float value = 0.0f) {
  const bool queued = QueueGuestTouch(gesture, x, y, value);
  const uint32_t count = g_received.fetch_add(1, std::memory_order_relaxed) + 1;
  // Log enough to prove the Android -> Wine hop without turning a drag into a
  // per-frame log storm. The first packets identify a dead overlay instantly;
  // the periodic counter confirms long gestures remain ordered.
  if (count <= 4 || (count % 120u) == 0u) {
    DXS_INFO("[touch] received #%u %s at %.3f,%.3f value=%.3f queued=%d",
             count, GestureName(gesture), x, y, value, queued ? 1 : 0);
  }
  return queued;
}

DWORD WINAPI TouchThread(void*) {
  char packet[128];
  while (g_running.load(std::memory_order_acquire)) {
    const SOCKET receiver = g_socket.load(std::memory_order_acquire);
    if (receiver == INVALID_SOCKET) break;
    sockaddr_in sender = {};
    int sender_size = sizeof(sender);
    const int length = recvfrom(receiver, packet, sizeof(packet) - 1, 0,
                                reinterpret_cast<sockaddr*>(&sender), &sender_size);
    if (length <= 0) {
      if (!g_running.load(std::memory_order_acquire)) break;
      continue;
    }
    if (std::memchr(packet, '\0', static_cast<size_t>(length)) != nullptr) {
      const uint32_t rejected =
          g_rejected.fetch_add(1, std::memory_order_relaxed) + 1;
      if (rejected <= 3) {
        DXS_WARN("[touch] rejected guest gesture with embedded NUL #%u",
                 rejected);
      }
      continue;
    }
    packet[length] = '\0';
    if (!ValidFeedbackPeer(sender, sender_size)) {
      const uint32_t rejected =
          g_rejected.fetch_add(1, std::memory_order_relaxed) + 1;
      if (rejected <= 3) {
        DXS_WARN("[touch] rejected non-loopback or malformed sender #%u",
                 rejected);
      }
      continue;
    }

    float x = -1.0f;
    float y = -1.0f;
    float value = 0.0f;
    // WEMU appends the press duration to both tap and hold packets. Retain
    // compatibility with the older two-coordinate tap sender, but parse the
    // current four-token form exactly instead of treating its final field as
    // malformed trailing input.
    const bool tap_with_duration =
        ParseArguments(packet, "tap", &x, &y, &value);
    const bool tap_without_duration =
        !tap_with_duration && ParseArguments(packet, "tap", &x, &y);
    bool parsed = true;
    bool queued = false;
    if (ParseArguments(packet, "select", &x, &y) &&
        std::isfinite(x) && std::isfinite(y) &&
        x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f) {
      // Legacy senders used "select" for a coordinate, not an internal page
      // id. Route it through the same exact header geometry as a modern tap;
      // x*6 addressed the hidden HUD page and did not match the visible order
      // Inventory, Augmentations, Map, Objectives, Data.
      queued = QueueTouch(GuestGesture::kTap, x, y);
    } else if ((tap_with_duration || tap_without_duration) &&
        std::isfinite(x) && std::isfinite(y) &&
        x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f &&
        (!tap_with_duration || (std::isfinite(value) && value >= 0.0f))) {
      queued = QueueTouch(GuestGesture::kTap, x, y,
                          tap_with_duration ? value : 0.0f);
    } else if (ParseArguments(packet, "down", &x, &y) &&
               std::isfinite(x) && std::isfinite(y) &&
               x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f) {
      queued = QueueTouch(GuestGesture::kDown, x, y);
    } else if (ParseArguments(packet, "move", &x, &y) &&
               std::isfinite(x) && std::isfinite(y)) {
      queued = QueueTouch(GuestGesture::kMove, x, y);
    } else if (ParseArguments(packet, "up", &x, &y) &&
               std::isfinite(x) && std::isfinite(y)) {
      queued = QueueTouch(GuestGesture::kUp, x, y);
    } else if ((ParseArguments(packet, "cancel", &x, &y) &&
                std::isfinite(x) && std::isfinite(y)) ||
               IsCommandOnly(packet, "cancel")) {
      queued = QueueTouch(GuestGesture::kCancel, x, y);
    } else if (ParseArguments(packet, "hold", &x, &y, &value) &&
               std::isfinite(x) && std::isfinite(y) &&
               x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f &&
               std::isfinite(value) && value >= 0.0f) {
      queued = QueueTouch(GuestGesture::kHold, x, y, value);
    } else if (ParseArguments(packet, "pinch", &x, &y, &value) &&
               std::isfinite(x) && std::isfinite(y) &&
               x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f &&
               std::isfinite(value) && value > 0.0f) {
      // Android normally emits a small relative scale. Bound the untrusted
      // loopback packet before it reaches persistent map state: an infinite
      // or extreme value could otherwise pin the viewport at one limit and
      // make the next legitimate pinch appear broken.
      queued = QueueTouch(GuestGesture::kPinch, x, y,
                          (std::max)(0.25f, (std::min)(4.0f, value)));
    } else {
      parsed = false;
      const uint32_t rejected =
          g_rejected.fetch_add(1, std::memory_order_relaxed) + 1;
      if (rejected <= 3) {
        DXS_WARN("[touch] rejected malformed guest gesture #%u: %.80s",
                 rejected, packet);
      }
    }
    if (parsed && queued) PublishFeedbackPeer(sender);
  }
  return 0;
}

}  // namespace

bool StartTouchInput() {
  AcquireSRWLockExclusive(&g_lifecycle_lock);
  if (g_state == TouchState::kRunning) {
    ReleaseSRWLockExclusive(&g_lifecycle_lock);
    return true;
  }
  if (g_state == TouchState::kStopping) {
    if (!g_thread || WaitForSingleObject(g_thread, 0) != WAIT_OBJECT_0) {
      DXS_WARN("[touch] previous receiver is still stopping; not starting a second worker");
      ReleaseSRWLockExclusive(&g_lifecycle_lock);
      return false;
    }
    CloseHandle(g_thread);
    g_thread = nullptr;
    if (g_winsock_started) {
      WSACleanup();
      g_winsock_started = false;
    }
    g_state = TouchState::kStopped;
  }

  g_received.store(0, std::memory_order_relaxed);
  g_rejected.store(0, std::memory_order_relaxed);
  AcquireSRWLockExclusive(&g_feedback_lock);
  g_feedback_peer = {};
  g_feedback_peer_valid = false;
  ReleaseSRWLockExclusive(&g_feedback_lock);

  WSADATA data = {};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    DXS_WARN("[touch] Winsock startup failed; Thor panel touch is unavailable");
    ReleaseSRWLockExclusive(&g_lifecycle_lock);
    return false;
  }
  g_winsock_started = true;

  const SOCKET receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  g_socket.store(receiver, std::memory_order_release);
  if (receiver == INVALID_SOCKET) {
    DXS_WARN("[touch] could not create the loopback UDP socket (Winsock %d)",
             WSAGetLastError());
    WSACleanup();
    g_winsock_started = false;
    ReleaseSRWLockExclusive(&g_lifecycle_lock);
    return false;
  }
  // A bounded receive wait guarantees Stop can join an idle worker even on a
  // Winsock implementation which does not immediately wake recvfrom after a
  // close from another thread.
  const DWORD receive_timeout_ms = 250;
  if (setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&receive_timeout_ms),
                 sizeof(receive_timeout_ms)) == SOCKET_ERROR) {
    DXS_WARN("[touch] could not set the receiver timeout (Winsock %d); shutdown will rely on socket close",
             WSAGetLastError());
  }

  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(kTouchPort);
  if (bind(receiver, reinterpret_cast<const sockaddr*>(&address),
           sizeof(address)) == SOCKET_ERROR) {
    DXS_WARN("[touch] could not bind 127.0.0.1:%u (Winsock %d)",
             kTouchPort, WSAGetLastError());
    closesocket(receiver);
    g_socket.store(INVALID_SOCKET, std::memory_order_release);
    WSACleanup();
    g_winsock_started = false;
    ReleaseSRWLockExclusive(&g_lifecycle_lock);
    return false;
  }

  g_running.store(true, std::memory_order_release);
  g_thread = CreateThread(nullptr, 0, TouchThread, nullptr, 0, nullptr);
  if (!g_thread) {
    DXS_WARN("[touch] could not create the receiver thread (Windows %lu)",
             GetLastError());
    g_running.store(false, std::memory_order_release);
    closesocket(receiver);
    g_socket.store(INVALID_SOCKET, std::memory_order_release);
    WSACleanup();
    g_winsock_started = false;
    ReleaseSRWLockExclusive(&g_lifecycle_lock);
    return false;
  }
  g_state = TouchState::kRunning;
  DXS_INFO("[touch] listening for Thor panel gestures on 127.0.0.1:%u", kTouchPort);
  ReleaseSRWLockExclusive(&g_lifecycle_lock);
  return true;
}

void StopTouchInput() {
  AcquireSRWLockExclusive(&g_lifecycle_lock);
  if (g_state == TouchState::kStopped) {
    ReleaseSRWLockExclusive(&g_lifecycle_lock);
    return;
  }
  g_state = TouchState::kStopping;
  g_running.store(false, std::memory_order_release);

  // SendGuestHaptic holds this lock while it reads and uses the socket. Closing
  // under the exclusive side makes the handle lifetime atomic with sendto.
  AcquireSRWLockExclusive(&g_feedback_lock);
  const SOCKET socket_to_close =
      g_socket.exchange(INVALID_SOCKET, std::memory_order_acq_rel);
  g_feedback_peer = {};
  g_feedback_peer_valid = false;
  if (socket_to_close != INVALID_SOCKET) closesocket(socket_to_close);
  ReleaseSRWLockExclusive(&g_feedback_lock);

  if (g_thread) {
    const DWORD joined = WaitForSingleObject(g_thread, 2000);
    if (joined != WAIT_OBJECT_0) {
      DXS_WARN("[touch] receiver thread did not stop (wait=%lu); retaining its resources",
               joined);
      ReleaseSRWLockExclusive(&g_lifecycle_lock);
      return;
    }
    CloseHandle(g_thread);
    g_thread = nullptr;
  }
  if (g_winsock_started) {
    WSACleanup();
    g_winsock_started = false;
  }
  g_state = TouchState::kStopped;
  ReleaseSRWLockExclusive(&g_lifecycle_lock);
}

bool SendGuestHaptic(GuestHapticCue cue) {
  const char* message = nullptr;
  switch (cue) {
    case GuestHapticCue::kSelection: message = "haptic selection"; break;
    case GuestHapticCue::kAction: message = "haptic action"; break;
    case GuestHapticCue::kUnlock: message = "haptic unlock"; break;
  }
  if (!message || !g_running.load(std::memory_order_acquire)) return false;

  bool sent = false;
  AcquireSRWLockShared(&g_feedback_lock);
  const SOCKET receiver = g_socket.load(std::memory_order_acquire);
  if (receiver != INVALID_SOCKET && g_feedback_peer_valid) {
    const int length = static_cast<int>(std::strlen(message));
    sent = sendto(receiver, message, length, 0,
                  reinterpret_cast<const sockaddr*>(&g_feedback_peer),
                  sizeof(g_feedback_peer)) == length;
  }
  ReleaseSRWLockShared(&g_feedback_lock);
  return sent;
}

}  // namespace dxs
