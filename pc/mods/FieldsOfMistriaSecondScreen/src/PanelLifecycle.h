#pragma once

#include <windows.h>

namespace fomds {

struct StartupHandleDecision {
  bool event_already_closed = false;
  bool close_event_after_join = false;
  bool close_thread_after_join = false;
  bool retain_event = false;
  bool retain_thread = false;
};

constexpr StartupHandleDecision DecideStartupHandles(DWORD startup_wait,
                                                      DWORD stop_wait) {
  const bool startup_signaled = startup_wait == WAIT_OBJECT_0;
  const bool thread_joined = stop_wait == WAIT_OBJECT_0;
  return StartupHandleDecision{
      startup_signaled,
      !startup_signaled && thread_joined,
      thread_joined,
      !startup_signaled && !thread_joined,
      !thread_joined,
  };
}

}  // namespace fomds
