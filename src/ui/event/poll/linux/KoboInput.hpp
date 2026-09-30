// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

namespace UI {

enum class KoboInputRole { TOUCH, POWER };
enum class KoboInputResult { UNSUITABLE, OPENED, FAILED };

struct KoboInputCapabilities {
  bool xy = false;
  bool mt_xy = false;
  bool tracking_id = false;
  bool slots = false;
  bool touch = false;
  bool direct = false;
  bool pen = false;
  bool power = false;

  constexpr bool Matches(KoboInputRole role) const noexcept {
    if (role == KoboInputRole::POWER)
      return power;

    // Direct multitouch controllers such as cyttsp5_mt may advertise pen
    // keys on the finger-input node. Reject pen-only nodes, not those nodes.
    if (pen && !(direct && mt_xy))
      return false;

    return (xy && touch) ||
      // Type-A multitouch uses BTN_TOUCH without a slot axis.
      (mt_xy && touch && !slots) ||
      (mt_xy && slots && tracking_id && (touch || direct));
  }
};

bool ReadKoboInputCapabilities(int fd, KoboInputCapabilities &caps) noexcept;

using KoboInputCandidate = KoboInputResult (*)(const char *path,
                                             void *context) noexcept;

/** A failed open/grab of a suitable device stops discovery. An explicit
    override is always authoritative, including when it fails. */
bool DiscoverKoboInput(KoboInputRole role, const char *override_path,
                       KoboInputCandidate candidate, void *context,
                       const char *directory = "/dev/input") noexcept;

} // namespace UI
