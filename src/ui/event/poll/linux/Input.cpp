// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Input.hpp"
#include "MergeMouse.hpp"
#include "ui/event/shared/Event.hpp"
#include "ui/event/Queue.hpp"
#include "io/FileDescriptor.hxx"
#include "Asset.hpp"
#include "Translate.hpp"

#include <algorithm>

#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <linux/input.h>

template<typename T>
static constexpr unsigned
BitSize() noexcept
{
  return 8 * sizeof(T);
}

template<typename T>
static constexpr size_t
BitsToInts(unsigned n_bits) noexcept
{
  return (n_bits + BitSize<T>() - 1) / BitSize<T>();
}

template<typename T>
static constexpr bool
CheckBit(const T bits[], unsigned i) noexcept
{
  return bits[i / BitSize<T>()] & (T(1) << (i % BitSize<T>()));
}

namespace UI {

LinuxInputDevice::LinuxInputDevice(EventQueue &_queue,
                                   MergeMouse &_merge) noexcept
  :queue(_queue), merge(_merge),
   edit_position(0, 0), public_position(0, 0),
#ifdef TARGET_IS_KOBO_NICKEL
   grabbed(false),
#endif
   event(queue.GetEventLoop(), BIND_THIS_METHOD(OnSocketReady))
{
}

/**
 * Check if the evdev device supports EV_ABS or EV_REL.
 */
static bool
IsPointerDevice(int fd) noexcept
{
  assert(fd >= 0);

  unsigned long features[BitsToInts<unsigned long>(EV_MAX + 1)]{};
  if (ioctl(fd, EVIOCGBIT(0, sizeof(features)), features) < 0)
    return false;

  return CheckBit(features, EV_ABS) || CheckBit(features, EV_REL);
}

bool
LinuxInputDevice::Open(const char *path) noexcept
{
  Close();
  FileDescriptor _fd;
  if (!_fd.OpenReadOnly(path))
    return false;

  return Open(_fd, path);
}

bool
LinuxInputDevice::Open(FileDescriptor _fd, const char *path) noexcept
{
  Close();
  _fd.SetNonBlocking();
  event.Open(_fd);

  min_x = max_x = min_y = max_y = 0;

  is_pointer = IsPointerDevice(event.GetFileDescriptor().Get());
  if (is_pointer) {
    merge.AddPointer();

    if (!IsKobo()) {
      /* Obtain axis ranges on non-Kobo devices. Kobo coordinates are already
         calibrated to the display, so retain their existing transform. */

      const int fd = event.GetFileDescriptor().Get();

      input_absinfo abs;
      if (ioctl(fd, EVIOCGABS(ABS_X), &abs) == 0) {
        min_x = abs.minimum;
        max_x = abs.maximum;
      }

      if (ioctl(fd, EVIOCGABS(ABS_Y), &abs) == 0) {
        min_y = abs.minimum;
        max_y = abs.maximum;
      }
    }

#ifdef TARGET_IS_KOBO_NICKEL
    /* Own the input exclusively until the launcher resumes Nickel. */
    grabbed = ioctl(event.GetFileDescriptor().Get(), EVIOCGRAB, 1) == 0;
    if (!grabbed) {
      fprintf(stderr, "Failed to grab input %s: %s\n", path, strerror(errno));
      Close();
      return false;
    }
#endif
  }

  rel_x = rel_y = rel_wheel = 0;
  down = false;
  moving = pressing = releasing = false;
  if (!event.ScheduleRead()) {
    fprintf(stderr, "Cannot monitor input %s: %s\n", path, strerror(errno));
    Close();
    return false;
  }
  return true;
}

#ifdef TARGET_IS_KOBO_NICKEL
KoboInputResult
LinuxInputDevice::OpenKoboCandidate(const char *path, KoboInputRole role,
                                    const LinuxInputDevice *exclude) noexcept
{
  /* Reject a known alias of an already owned event node before opening it. */
  if (exclude != nullptr && exclude->IsOpen()) {
    struct stat target, other;
    if (stat(path, &target) == 0 && S_ISCHR(target.st_mode) &&
        fstat(exclude->event.GetFileDescriptor().Get(), &other) == 0 &&
        target.st_rdev == other.st_rdev)
      return KoboInputResult::UNSUITABLE;
  }
  FileDescriptor fd;
  if (!fd.OpenReadOnly(path)) {
    if (errno != ENOENT)
      fprintf(stderr, "Failed to open input %s: %s\n", path, strerror(errno));
    return KoboInputResult::UNSUITABLE;
  }

  struct stat identity;
  if (fstat(fd.Get(), &identity) < 0 || !S_ISCHR(identity.st_mode)) {
    fd.Close();
    return KoboInputResult::UNSUITABLE;
  }
  if (exclude != nullptr && exclude->IsOpen()) {
    struct stat other;
    if (fstat(exclude->event.GetFileDescriptor().Get(), &other) == 0 &&
        identity.st_rdev == other.st_rdev) {
      fd.Close();
      return KoboInputResult::UNSUITABLE;
    }
  }

  KoboInputCapabilities caps;
  const bool queried = ReadKoboInputCapabilities(fd.Get(), caps);
  if (queried) {
    char name[128]{};
    ioctl(fd.Get(), EVIOCGNAME(sizeof(name)), name);
    fprintf(stderr, "Kobo input %s (%.*s): xy=%d mt_xy=%d slots=%d "
            "tracking_id=%d BTN_TOUCH=%d direct=%d pen=%d KEY_POWER=%d\n",
            path, int(sizeof(name) - 1), name, caps.xy, caps.mt_xy,
            caps.slots, caps.tracking_id, caps.touch, caps.direct,
            caps.pen, caps.power);
  }
  const bool suitable = queried && caps.Matches(role);
  if (!suitable) {
    if (!queried)
      fprintf(stderr, "Cannot query input capabilities %s: %s\n", path, strerror(errno));
    else
      fprintf(stderr, "Input %s lacks Kobo %s capabilities\n", path,
              role == KoboInputRole::TOUCH ? "touch" : "power");
    fd.Close();
    return KoboInputResult::UNSUITABLE;
  }

  /* Open performs the grab; failure is fatal to selection, so Nickel can
     never compete with XCSoar for a device which failed EVIOCGRAB. */
  if (!Open(fd, path))
    return KoboInputResult::FAILED;

  /* Power must also be owned exclusively while Nickel is suspended. */
  if (!grabbed) {
    grabbed = ioctl(event.GetFileDescriptor().Get(), EVIOCGRAB, 1) == 0;
    if (!grabbed) {
      fprintf(stderr, "Failed to grab input %s: %s\n", path, strerror(errno));
      Close();
      return KoboInputResult::FAILED;
    }
  }
  mt_only = !(caps.xy && caps.touch);
  mt_slot_protocol = mt_only && caps.slots && caps.tracking_id;
  mt_slot = 0;
  if (role == KoboInputRole::TOUCH) {
    fprintf(stderr, "Kobo touch input: %s (%s coordinates)\n", path,
            mt_only ? (mt_slot_protocol ? "multitouch slots" : "multitouch reports")
                    : "legacy");

    /* Log ranges without changing Kobo's calibrated coordinate transform. */
    for (unsigned axis : {mt_only ? ABS_MT_POSITION_X : ABS_X,
                          mt_only ? ABS_MT_POSITION_Y : ABS_Y}) {
      input_absinfo abs;
      if (ioctl(event.GetFileDescriptor().Get(), EVIOCGABS(axis), &abs) == 0)
        fprintf(stderr, "Kobo input axis %u: min=%d max=%d\n",
                axis, abs.minimum, abs.maximum);
    }
  } else
    fprintf(stderr, "Kobo power input: %s\n", path);
  return KoboInputResult::OPENED;
}

bool
LinuxInputDevice::OpenKobo(KoboInputRole role, const char *override_path,
                          const LinuxInputDevice *exclude) noexcept
{
  struct Context {
    LinuxInputDevice &device;
    KoboInputRole role;
    const LinuxInputDevice *exclude;
  } context{*this, role, exclude};
  return DiscoverKoboInput(role, override_path,
    [](const char *path, void *opaque) noexcept {
      auto &c = *static_cast<Context *>(opaque);
      return c.device.OpenKoboCandidate(path, c.role, c.exclude);
    }, &context);
}
#endif

void
LinuxInputDevice::Close() noexcept
{
  if (!IsOpen())
    return;

#ifdef TARGET_IS_KOBO_NICKEL
  if (grabbed) {
    ioctl(event.GetFileDescriptor().Get(), EVIOCGRAB, 0);
    grabbed = false;
  }
#endif
  if (is_pointer) {
    merge.RemovePointer();
  }

  event.Close();
}

inline void
LinuxInputDevice::Read() noexcept
{
  FileDescriptor fd = event.GetFileDescriptor();

  struct input_event buffer[64];
  const auto nbytes = fd.Read(std::as_writable_bytes(std::span{buffer}));
  if (nbytes < 0) {
    /* device has failed or was unplugged - bail out */
    if (errno != EAGAIN && errno != EINTR)
      Close();
    return;
  }

  unsigned n = size_t(nbytes) / sizeof(buffer[0]);

  for (unsigned i = 0; i < n; ++i) {
    const struct input_event &e = buffer[i];

    switch (e.type) {
    case EV_SYN:
      if (e.code == SYN_REPORT) {
        /* commit the finger movement */

        const bool pressed = pressing;
        const bool released = releasing;
        pressing = releasing = false;

        if (pressed)
          merge.SetDown(true);

        if (released)
          merge.SetDown(false);

        if (IsKobo() && released) {
          /* workaround: on the Kobo Touch N905B, releasing the touch
             screen reliably produces a finger position that is way
             off; in that case, ignore finger movement */
          moving = false;
          edit_position = public_position;
        }

        if (moving) {
          moving = false;
          public_position = edit_position;
          merge.MoveAbsolute(public_position.x, public_position.y,
                             min_x, max_x, min_y, max_y);
        } else if (rel_x != 0 || rel_y != 0) {
          merge.MoveRelative(PixelPoint(rel_x, rel_y));
          rel_x = rel_y = 0;
        }

        if (rel_wheel != 0) {
          merge.MoveWheel(rel_wheel);
          rel_wheel = 0;
        }

        queue.WakeUp();
      }

      break;

    case EV_KEY:
#ifdef TARGET_IS_KOBO_NICKEL
      if (mt_slot_protocol && e.code == BTN_TOUCH)
        break; // tracking IDs own the primary contact in the slot protocol
#endif
      if (e.code == BTN_TOUCH || e.code == BTN_MOUSE) {
        bool new_down = e.value;
        if (new_down != down) {
          down = new_down;
          if (new_down)
            pressing = true;
          else
            releasing = true;
        }
      } else {
        /* Discard all data on stdin to avoid that keyboard input data is read
         * on the executing shell. This fixes #3403. */
        tcflush(STDIN_FILENO, TCIFLUSH);

        const auto [translated_key_code, is_char] = TranslateKeyCode(e.code);
        Event ev(e.value ? Event::KEY_DOWN : Event::KEY_UP,
                 translated_key_code);
        ev.is_char = is_char;
        queue.Push(ev);
      }

      break;

    case EV_ABS:
#ifdef TARGET_IS_KOBO_NICKEL
      if (e.code == ABS_MT_SLOT) {
        if (mt_slot_protocol)
          mt_slot = unsigned(e.value);
        break;
      }
      if (e.code >= ABS_MT_TOUCH_MAJOR &&
          (!mt_only || (mt_slot_protocol && mt_slot != 0)))
        break;
      if (mt_slot_protocol && e.code == ABS_MT_TRACKING_ID) {
        const bool new_down = e.value >= 0;
        if (new_down != down) {
          down = new_down;
          if (down)
            pressing = true;
          else
            releasing = true;
        }
        break;
      }
#endif
      moving = true;

      switch (e.code) {
      case ABS_X:
        edit_position.x = e.value;
        break;

      case ABS_Y:
        edit_position.y = e.value;
        break;

      case ABS_MT_POSITION_X:
        edit_position.x = e.value;
        break;

      case ABS_MT_POSITION_Y:
        edit_position.y = e.value;
        break;
      }

      break;

    case EV_REL:
      switch (e.code) {
      case REL_X:
        rel_x += e.value;
        break;

      case REL_Y:
        rel_y += e.value;
        break;

      case REL_WHEEL:
        rel_wheel += e.value;
        break;
      }

      break;
    }
  }
}

void
LinuxInputDevice::OnSocketReady(unsigned) noexcept
{
  Read();
}

} // namespace UI
