// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "InputQueue.hpp"
#include "../shared/Event.hpp"
#include "DisplayOrientation.hpp"
#ifdef TARGET_IS_KOBO_NICKEL
#include <stdio.h>
#include <stdlib.h>
#endif

namespace UI {

InputEventQueue::InputEventQueue(EventQueue &queue) noexcept
  :
#ifndef USE_LIBINPUT
   keyboard(queue, merge_mouse),
   mouse(queue, merge_mouse)
#else
   libinput_handler(queue)
#endif
{
#ifndef USE_LIBINPUT
#ifdef TARGET_IS_KOBO_NICKEL
  if (!mouse.OpenKobo(KoboInputRole::TOUCH, getenv("KOBO_TOUCH_DEVICE"))) {
    fprintf(stderr, "No usable Kobo touch input; exiting\n");
    exit(EXIT_FAILURE);
  }
  if (!keyboard.OpenKobo(KoboInputRole::POWER, getenv("KOBO_POWER_DEVICE"), &mouse))
    fprintf(stderr, "No separate Kobo power input found\n");
#else
  /* power button */
  keyboard.Open("/dev/input/event0");

  /* Kobo touch screen */
  mouse.Open("/dev/input/event1");
#endif
#else
  libinput_handler.Open();
#endif
}

InputEventQueue::~InputEventQueue() noexcept = default;

bool
InputEventQueue::Generate([[maybe_unused]] Event &event) noexcept
{
#ifndef USE_LIBINPUT
  event = merge_mouse.Generate();
  if (event.type != Event::Type::NOP)
    return true;
#endif

  return false;
}

} // namespace UI
