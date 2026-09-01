#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Log.h"
#include "Panel.h"
#include "TouchInput.h"

namespace dis {
namespace {

constexpr unsigned short kTouchPort = 7788;
constexpr unsigned kQueueCapacity = 128;

enum class Gesture : unsigned char {
  kTap,
  kHold,
  kDown,
  kMove,
  kUp,
  kPinch,
  kCancel,
};

struct TouchEvent {
  Gesture gesture = Gesture::kTap;
  float x = 0.0f;
  float y = 0.0f;
  float value = 0.0f;
};

std::atomic<bool> g_running{false};
std::atomic<unsigned> g_received{0};
std::atomic<unsigned> g_dropped{0};
std::atomic<SOCKET> g_socket{INVALID_SOCKET};
HANDLE g_thread = nullptr;
SRWLOCK g_queue_lock = SRWLOCK_INIT;
TouchEvent g_queue[kQueueCapacity] = {};
unsigned g_read = 0;
unsigned g_write = 0;
unsigned g_count = 0;

const char* GestureName(Gesture gesture) {
  switch (gesture) {
    case Gesture::kTap: return "tap";
    case Gesture::kHold: return "hold";
    case Gesture::kDown: return "down";
    case Gesture::kMove: return "move";
    case Gesture::kUp: return "up";
    case Gesture::kPinch: return "pinch";
    case Gesture::kCancel: return "cancel";
  }
  return "unknown";
}

bool CommandOnly(const char* packet, const char* command) {
  if (!packet || !command) return false;
  const size_t length = std::strlen(command);
  if (std::strncmp(packet, command, length) != 0) return false;
  const char* tail = packet + length;
  tail += std::strspn(tail, " \t\r\n");
  return *tail == '\0';
}

void Enqueue(Gesture gesture, float x, float y, float value = 0.0f) {
  AcquireSRWLockExclusive(&g_queue_lock);

  // A drag may deliver more moves than the render thread can consume. Replace
  // the newest pending move instead of allowing old coordinates to delay the
  // final release or page tap.
  if (gesture == Gesture::kMove && g_count != 0) {
    const unsigned last = (g_write + kQueueCapacity - 1) % kQueueCapacity;
    if (g_queue[last].gesture == Gesture::kMove) {
      g_queue[last] = {gesture, x, y, value};
      ReleaseSRWLockExclusive(&g_queue_lock);
      return;
    }
  }

  if (g_count == kQueueCapacity) {
    g_read = (g_read + 1) % kQueueCapacity;
    --g_count;
    g_dropped.fetch_add(1, std::memory_order_relaxed);
  }
  g_queue[g_write] = {gesture, x, y, value};
  g_write = (g_write + 1) % kQueueCapacity;
  ++g_count;
  ReleaseSRWLockExclusive(&g_queue_lock);

  const unsigned received =
      g_received.fetch_add(1, std::memory_order_relaxed) + 1;
  if (received <= 6 || (received % 120u) == 0u) {
    if (gesture == Gesture::kPinch || gesture == Gesture::kHold) {
      DIS_INFO("[touch] received #%u %s at %.3f,%.3f value %.3f", received,
               GestureName(gesture), x, y, value);
    } else {
      DIS_INFO("[touch] received #%u %s at %.3f,%.3f", received,
               GestureName(gesture), x, y);
    }
  }
}

DWORD WINAPI TouchThread(void*) {
  char packet[128];
  while (g_running.load(std::memory_order_acquire)) {
    const SOCKET receiver = g_socket.load(std::memory_order_acquire);
    if (receiver == INVALID_SOCKET) break;
    sockaddr_in sender = {};
    int sender_size = sizeof(sender);
    const int length = recvfrom(receiver, packet, sizeof(packet) - 1, 0,
                                reinterpret_cast<sockaddr*>(&sender),
                                &sender_size);
    if (length <= 0) {
      if (!g_running.load(std::memory_order_acquire)) break;
      continue;
    }
    packet[length] = '\0';

    float x = -1.0f;
    float y = -1.0f;
    float value = 0.0f;
    if ((std::sscanf(packet, "select %f %f", &x, &y) == 2 ||
         std::sscanf(packet, "tap %f %f", &x, &y) == 2) &&
        x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f) {
      Enqueue(Gesture::kTap, x, y);
    } else if (std::sscanf(packet, "hold %f %f %f", &x, &y, &value) >= 2 &&
               x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f) {
      Enqueue(Gesture::kHold, x, y,
              std::isfinite(value) ? std::max(0.0f, value) : 0.0f);
    } else if (std::sscanf(packet, "down %f %f", &x, &y) == 2 &&
               x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f) {
      Enqueue(Gesture::kDown, x, y);
    } else if (std::sscanf(packet, "move %f %f", &x, &y) == 2 &&
               std::isfinite(x) && std::isfinite(y)) {
      Enqueue(Gesture::kMove, x, y);
    } else if (std::sscanf(packet, "up %f %f", &x, &y) == 2 &&
               std::isfinite(x) && std::isfinite(y)) {
      Enqueue(Gesture::kUp, x, y);
    } else if (std::sscanf(packet, "pinch %f %f %f", &x, &y, &value) == 3 &&
               x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f &&
               std::isfinite(value) && value > 0.0f) {
      Enqueue(Gesture::kPinch, x, y, std::clamp(value, 0.25f, 4.0f));
    } else if (std::sscanf(packet, "cancel %f %f", &x, &y) == 2 ||
               CommandOnly(packet, "cancel")) {
      Enqueue(Gesture::kCancel, x, y);
    }
  }
  return 0;
}

}  // namespace

bool StartTouchInput() {
  bool expected = false;
  if (!g_running.compare_exchange_strong(expected, true)) return true;

  AcquireSRWLockExclusive(&g_queue_lock);
  g_read = g_write = g_count = 0;
  ReleaseSRWLockExclusive(&g_queue_lock);
  g_received.store(0, std::memory_order_relaxed);
  g_dropped.store(0, std::memory_order_relaxed);

  WSADATA data = {};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    g_running.store(false, std::memory_order_release);
    DIS_WARN("[touch] Winsock startup failed; Thor touch is unavailable");
    return false;
  }

  const SOCKET receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  g_socket.store(receiver, std::memory_order_release);
  if (receiver == INVALID_SOCKET) {
    WSACleanup();
    g_running.store(false, std::memory_order_release);
    return false;
  }

  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(kTouchPort);
  if (bind(receiver, reinterpret_cast<const sockaddr*>(&address),
           sizeof(address)) == SOCKET_ERROR) {
    DIS_WARN("[touch] could not bind 127.0.0.1:%u (Winsock %d)",
             kTouchPort, WSAGetLastError());
    closesocket(receiver);
    g_socket.store(INVALID_SOCKET, std::memory_order_release);
    WSACleanup();
    g_running.store(false, std::memory_order_release);
    return false;
  }

  g_thread = CreateThread(nullptr, 0, TouchThread, nullptr, 0, nullptr);
  if (!g_thread) {
    closesocket(receiver);
    g_socket.store(INVALID_SOCKET, std::memory_order_release);
    WSACleanup();
    g_running.store(false, std::memory_order_release);
    return false;
  }
  DIS_INFO("[touch] listening for Thor panel gestures on 127.0.0.1:%u",
           kTouchPort);
  return true;
}

void StopTouchInput() {
  if (!g_running.exchange(false)) return;
  const SOCKET socket_to_close =
      g_socket.exchange(INVALID_SOCKET, std::memory_order_acq_rel);
  if (socket_to_close != INVALID_SOCKET) closesocket(socket_to_close);
  if (g_thread) {
    WaitForSingleObject(g_thread, 1000);
    CloseHandle(g_thread);
    g_thread = nullptr;
  }
  WSACleanup();
}

void PumpTouchInput(Panel& panel) {
  constexpr unsigned kMaximumPerFrame = 32;
  for (unsigned i = 0; i < kMaximumPerFrame; ++i) {
    TouchEvent event;
    bool available = false;
    AcquireSRWLockExclusive(&g_queue_lock);
    if (g_count != 0) {
      event = g_queue[g_read];
      g_read = (g_read + 1) % kQueueCapacity;
      --g_count;
      available = true;
    }
    ReleaseSRWLockExclusive(&g_queue_lock);
    if (!available) break;

    switch (event.gesture) {
      case Gesture::kTap:
        panel.HandleNormalizedTap(event.x, event.y);
        break;
      case Gesture::kHold:
        panel.HandleNormalizedHold(event.x, event.y);
        break;
      case Gesture::kDown:
        panel.HandleNormalizedPointer(WM_LBUTTONDOWN, event.x, event.y);
        break;
      case Gesture::kMove:
        panel.HandleNormalizedPointer(WM_MOUSEMOVE, event.x, event.y);
        break;
      case Gesture::kUp:
        panel.HandleNormalizedPointer(WM_LBUTTONUP, event.x, event.y);
        break;
      case Gesture::kPinch:
        panel.HandleNormalizedPinch(event.x, event.y, event.value);
        break;
      case Gesture::kCancel:
        panel.CancelPointer();
        break;
    }
  }
}

}  // namespace dis
