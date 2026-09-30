// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "event/PipeEvent.hxx"
#include "Math/Point2D.hpp"
#ifdef TARGET_IS_KOBO_NICKEL
#include "KoboInput.hpp"
#endif

class FileDescriptor;

namespace UI {

class EventQueue;
class MergeMouse;
struct Event;

/**
 * A driver for Linux input devices (/dev/input/event*).
 */
class LinuxInputDevice final {
  typedef IntPoint2D Position;

  EventQueue &queue;

  MergeMouse &merge;

  int min_x, max_x, min_y, max_y;

  /**
   * The position being edited.  Upon EV_SYN, it will be copied to
   * #moved_position if #moving is true.
   */
  Position edit_position;

  /**
   * The position published by Generate().
   */
  Position public_position;

  int rel_x, rel_y, rel_wheel;

  bool down;

  /**
   * Was #edit_position modified?
   */
  bool moving;

  /**
   * Was the finger pressed or released, but not yet committed with
   * EV_SYN/SYN_REPORT?
   */
  bool pressing, releasing;

  bool is_pointer;

#ifdef TARGET_IS_KOBO_NICKEL
  bool grabbed;
  bool mt_only = false;
  bool mt_slot_protocol = false;
  unsigned mt_slot = 0;
#endif

  PipeEvent event;

public:
  LinuxInputDevice(EventQueue &_queue, MergeMouse &_merge) noexcept;

  ~LinuxInputDevice() noexcept {
    Close();
  }

  bool Open(const char *path) noexcept;
#ifdef TARGET_IS_KOBO_NICKEL
  bool OpenKobo(KoboInputRole role, const char *override_path,
               const LinuxInputDevice *exclude = nullptr) noexcept;
#endif
  void Close() noexcept;

  bool IsOpen() const noexcept {
    return event.IsDefined();
  }

private:
  bool Open(FileDescriptor fd, const char *path) noexcept;
#ifdef TARGET_IS_KOBO_NICKEL
  KoboInputResult OpenKoboCandidate(const char *path, KoboInputRole role,
                                    const LinuxInputDevice *exclude) noexcept;
#endif
  void Read() noexcept;

  void OnSocketReady(unsigned events) noexcept;
};

} // namespace UI
