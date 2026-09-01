#include <cassert>
#include <cstdio>

#include "PanelLifecycle.h"

int main() {
  using fomds::DecideStartupHandles;
  const auto signaled_joined =
      DecideStartupHandles(WAIT_OBJECT_0, WAIT_OBJECT_0);
  assert(signaled_joined.event_already_closed);
  assert(!signaled_joined.close_event_after_join);
  assert(signaled_joined.close_thread_after_join);

  const auto timed_out_joined =
      DecideStartupHandles(WAIT_TIMEOUT, WAIT_OBJECT_0);
  assert(!timed_out_joined.event_already_closed);
  assert(timed_out_joined.close_event_after_join);
  assert(timed_out_joined.close_thread_after_join);
  assert(!timed_out_joined.retain_event && !timed_out_joined.retain_thread);

  const auto timed_out_running =
      DecideStartupHandles(WAIT_TIMEOUT, WAIT_TIMEOUT);
  assert(!timed_out_running.event_already_closed);
  assert(!timed_out_running.close_event_after_join);
  assert(!timed_out_running.close_thread_after_join);
  assert(timed_out_running.retain_event && timed_out_running.retain_thread);

  const auto signaled_running =
      DecideStartupHandles(WAIT_OBJECT_0, WAIT_TIMEOUT);
  assert(signaled_running.event_already_closed);
  assert(!signaled_running.retain_event && signaled_running.retain_thread);
  std::puts("PanelLifecycleTests passed: timeout retains signal/thread ownership");
  return 0;
}
