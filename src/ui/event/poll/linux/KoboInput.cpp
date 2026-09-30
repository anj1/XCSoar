// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "KoboInput.hpp"

#include <linux/input.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <initializer_list>
#include <errno.h>

namespace UI {

template<unsigned N>
struct InputBits {
  unsigned long bits[(N + sizeof(unsigned long) * 8) /
                     (sizeof(unsigned long) * 8)]{};

  bool Has(unsigned bit) const noexcept {
    return bits[bit / (sizeof(unsigned long) * 8)] &
      (1UL << (bit % (sizeof(unsigned long) * 8)));
  }
};

bool
ReadKoboInputCapabilities(int fd, KoboInputCapabilities &caps) noexcept
{
  InputBits<EV_MAX> events;
  InputBits<KEY_MAX> keys;
  InputBits<ABS_MAX> axes;
  InputBits<INPUT_PROP_MAX> properties;
  if (ioctl(fd, EVIOCGBIT(0, sizeof(events.bits)), events.bits) < 0 ||
      (events.Has(EV_KEY) &&
       ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys.bits)), keys.bits) < 0))
    return false;

  caps = {};
  caps.power = keys.Has(KEY_POWER);
  caps.touch = keys.Has(BTN_TOUCH);
  caps.pen = keys.Has(BTN_TOOL_PEN) || keys.Has(BTN_TOOL_RUBBER);
  if (events.Has(EV_ABS)) {
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(axes.bits)), axes.bits) < 0)
      return false;
    caps.xy = axes.Has(ABS_X) && axes.Has(ABS_Y);
    caps.mt_xy = axes.Has(ABS_MT_POSITION_X) && axes.Has(ABS_MT_POSITION_Y);
    caps.tracking_id = axes.Has(ABS_MT_TRACKING_ID);
    caps.slots = axes.Has(ABS_MT_SLOT);
  }
  if (ioctl(fd, EVIOCGPROP(sizeof(properties.bits)), properties.bits) >= 0)
    caps.direct = properties.Has(INPUT_PROP_DIRECT);
  return true;
}

static int
IsEvent(const dirent64 *entry) noexcept
{
  if (strncmp(entry->d_name, "event", 5) != 0 || entry->d_name[5] == '\0')
    return 0;
  for (const char *p = entry->d_name + 5; *p; ++p)
    if (*p < '0' || *p > '9')
      return 0;
  return 1;
}

bool
DiscoverKoboInput(KoboInputRole role, const char *override_path,
                   KoboInputCandidate candidate, void *context,
                   const char *directory) noexcept
{
  if (override_path != nullptr && *override_path != '\0') {
    const bool success = candidate(override_path, context) == KoboInputResult::OPENED;
    if (!success)
      fprintf(stderr, "Invalid Kobo %s override: %s\n",
              role == KoboInputRole::TOUCH ? "touch" : "power", override_path);
    return success;
  }

  char path[PATH_MAX];
  if (role == KoboInputRole::TOUCH) {
    for (const char *alias : {"touchscreen", "touchscreen0", "tablet"}) {
      snprintf(path, sizeof(path), "%s/%s", directory, alias);
      const auto result = candidate(path, context);
      if (result != KoboInputResult::UNSUITABLE)
        return result == KoboInputResult::OPENED;
    }
  }

  /* 64-bit inode numbers also occur when testing ARM binaries on a host
     filesystem; don't let them make otherwise valid event entries overflow. */
  dirent64 **entries;
  const int count = scandir64(directory, &entries, IsEvent, versionsort64);
  if (count < 0) {
    fprintf(stderr, "Failed to scan Kobo input directory %s: %s\n",
            directory, strerror(errno));
    return false;
  }

  KoboInputResult result = KoboInputResult::UNSUITABLE;
  for (int i = 0; i < count; ++i) {
    if (result == KoboInputResult::UNSUITABLE) {
      snprintf(path, sizeof(path), "%s/%s", directory, entries[i]->d_name);
      result = candidate(path, context);
    }
    free(entries[i]);
  }
  free(entries);
  return result == KoboInputResult::OPENED;
}

} // namespace UI
