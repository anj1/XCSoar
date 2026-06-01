// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Loop.hpp"
#include "Queue.hpp"
#include "../shared/Event.hpp"
#include "ui/window/TopWindow.hpp"

#include <stdio.h>

namespace UI {

static void
LoopTrace(const char *msg) noexcept
{
  fprintf(stderr, "xcsoar trace: eventloop: %s\n", msg);
  fflush(stderr);
}

bool
EventLoop::Get(Event &event)
{
  LoopTrace("Get begin");
  if (queue.IsQuit())
    return false;

  if (bulk) {
    if (queue.Pop(event)) {
      LoopTrace("Get pop event");
      return true;
    }

    /* that was the last event for now, refresh the screen now */
    if (top_window != nullptr) {
      LoopTrace("Get before bulk Refresh");
      top_window->Refresh();
      LoopTrace("Get after bulk Refresh");
    }

    bulk = false;
  }

  if (queue.Wait(event)) {
    bulk = true;
    LoopTrace("Get wait event");
    return true;
  }

  LoopTrace("Get false");
  return false;
}

void
EventLoop::Dispatch(const Event &event)
{
  LoopTrace("Dispatch begin");
  if (event.type == Event::CALLBACK) {
    LoopTrace("Dispatch callback");
    event.callback(event.ptr);
  } else if (top_window != nullptr && event.type != Event::NOP) {
#ifndef NON_INTERACTIVE
    LoopTrace("Dispatch OnEvent");
    top_window->OnEvent(event);
#endif
  }
  LoopTrace("Dispatch end");
}

} // namespace UI
