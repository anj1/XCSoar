// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "ui/event/poll/linux/KoboInput.hpp"
#include "TestUtil.hpp"

#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <vector>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <cstdarg>
#include <cstring>
#include <cerrno>

enum class MockDevice {
  ACCELEROMETER, TOUCH, PEN, MULTITOUCH, MULTITOUCH_A,
  MULTITOUCH_A_TRACKING, CYTTSP5_MT, POWER, FAILED,
};
static MockDevice mock_device;

extern "C" int __wrap_ioctl(int fd, unsigned long request, ...);

extern "C" int
__wrap_ioctl([[maybe_unused]] int fd, unsigned long request, ...)
{
  if (mock_device == MockDevice::FAILED) {
    errno = ENOTTY;
    return -1;
  }
  va_list ap;
  va_start(ap, request);
  auto *bits = va_arg(ap, unsigned long *);
  va_end(ap);
  memset(bits, 0, _IOC_SIZE(request));
  auto set = [bits](unsigned bit) {
    bits[bit / (sizeof(unsigned long) * 8)] |=
      1UL << (bit % (sizeof(unsigned long) * 8));
  };
  switch (_IOC_NR(request)) {
  case 0x20:
    set(EV_KEY);
    if (mock_device != MockDevice::POWER)
      set(EV_ABS);
    break;
  case 0x20 + EV_KEY:
    if (mock_device == MockDevice::POWER || mock_device == MockDevice::CYTTSP5_MT)
      set(KEY_POWER);
    if (mock_device == MockDevice::TOUCH || mock_device == MockDevice::PEN ||
        mock_device == MockDevice::MULTITOUCH_A ||
        mock_device == MockDevice::MULTITOUCH_A_TRACKING ||
        mock_device == MockDevice::CYTTSP5_MT)
      set(BTN_TOUCH);
    if (mock_device == MockDevice::PEN || mock_device == MockDevice::CYTTSP5_MT)
      set(BTN_TOOL_PEN);
    break;
  case 0x20 + EV_ABS:
    if (mock_device == MockDevice::MULTITOUCH ||
        mock_device == MockDevice::MULTITOUCH_A ||
        mock_device == MockDevice::MULTITOUCH_A_TRACKING ||
        mock_device == MockDevice::CYTTSP5_MT) {
      set(ABS_MT_POSITION_X); set(ABS_MT_POSITION_Y);
      if (mock_device != MockDevice::MULTITOUCH_A)
        set(ABS_MT_TRACKING_ID);
      if (mock_device == MockDevice::MULTITOUCH)
        set(ABS_MT_SLOT);
      if (mock_device == MockDevice::CYTTSP5_MT) {
        set(ABS_X); set(ABS_Y);
      }
    } else {
      set(ABS_X); set(ABS_Y);
    }
    break;
  case 0x09:
    if (mock_device == MockDevice::MULTITOUCH || mock_device == MockDevice::CYTTSP5_MT)
      set(INPUT_PROP_DIRECT);
    break;
  default:
    return -1;
  }
  return 0;
}

using namespace UI;

struct Candidates {
  std::string selected;
  std::vector<std::string> tried;
  KoboInputResult result = KoboInputResult::OPENED;

  static KoboInputResult Try(const char *path, void *opaque) noexcept {
    auto &c = *static_cast<Candidates *>(opaque);
    c.tried.emplace_back(path);
    return path == c.selected ? c.result : KoboInputResult::UNSUITABLE;
  }
};

int
main()
{
  plan_tests(34);
  KoboInputCapabilities caps;
  for (auto device : {MockDevice::ACCELEROMETER, MockDevice::TOUCH,
                      MockDevice::PEN, MockDevice::MULTITOUCH,
                      MockDevice::MULTITOUCH_A, MockDevice::MULTITOUCH_A_TRACKING,
                      MockDevice::CYTTSP5_MT, MockDevice::POWER}) {
    mock_device = device;
    const bool expected_touch =
      device == MockDevice::TOUCH || device == MockDevice::MULTITOUCH ||
      device == MockDevice::MULTITOUCH_A || device == MockDevice::MULTITOUCH_A_TRACKING ||
      device == MockDevice::CYTTSP5_MT;
    ok1(ReadKoboInputCapabilities(42, caps) &&
        caps.Matches(KoboInputRole::TOUCH) == expected_touch &&
        caps.Matches(KoboInputRole::POWER) ==
          (device == MockDevice::POWER || device == MockDevice::CYTTSP5_MT));
  }
  // Exact capability combination reported by the firmware 6 Clara Colour.
  mock_device = MockDevice::CYTTSP5_MT;
  ok1(ReadKoboInputCapabilities(42, caps) && caps.xy && caps.mt_xy &&
      !caps.slots && caps.tracking_id && caps.touch && caps.direct &&
      caps.pen && caps.power);
  mock_device = MockDevice::FAILED;
  ok1(!ReadKoboInputCapabilities(42, caps));
  caps = {};
  ok1(!caps.Matches(KoboInputRole::TOUCH));
  caps.xy = true;
  ok1(!caps.Matches(KoboInputRole::TOUCH)); // accelerometer
  caps.touch = true;
  ok1(caps.Matches(KoboInputRole::TOUCH));
  caps.pen = true;
  ok1(!caps.Matches(KoboInputRole::TOUCH));
  caps.direct = true;
  ok1(!caps.Matches(KoboInputRole::TOUCH)); // direct pen-only node
  caps = {};
  caps.mt_xy = caps.tracking_id = caps.slots = caps.direct = true;
  ok1(caps.Matches(KoboInputRole::TOUCH));
  caps.tracking_id = false;
  ok1(!caps.Matches(KoboInputRole::TOUCH));
  caps.slots = false;
  caps.touch = true;
  ok1(caps.Matches(KoboInputRole::TOUCH));
  caps.pen = true;
  ok1(caps.Matches(KoboInputRole::TOUCH)); // combined direct finger/pen node
  caps.slots = caps.tracking_id = true;
  caps.touch = false;
  ok1(caps.Matches(KoboInputRole::TOUCH)); // combined slot-protocol node
  caps.direct = false;
  ok1(!caps.Matches(KoboInputRole::TOUCH));
  caps.power = true;
  ok1(caps.Matches(KoboInputRole::POWER));

  char directory[] = "/tmp/xcsoar-input-XXXXXX";
  if (mkdtemp(directory) == nullptr)
    abort();
  const std::string base = std::string{directory} + "/";
  for (const char *name : {"event0", "event2", "event10", "eventual"}) {
    const int fd = open((base + name).c_str(), O_CREAT | O_WRONLY, 0600);
    if (fd < 0)
      abort();
    close(fd);
  }
  Candidates c;
  auto discover = [&](const char *override_path = nullptr,
                       KoboInputRole role = KoboInputRole::TOUCH) {
    c.tried.clear();
    return DiscoverKoboInput(role, override_path, Candidates::Try, &c, directory);
  };
  c.selected = "/explicit/touch";
  ok1(discover(c.selected.c_str()));
  ok1(c.tried.size() == 1);
  ok1(!discover("/invalid/override"));
  ok1(c.tried.size() == 1);
  c.selected = base + "touchscreen0";
  ok1(discover());
  ok1(c.tried.size() == 2);
  c.result = KoboInputResult::FAILED; // grab failure: no silent fallback
  ok1(!discover());
  ok1(c.tried.size() == 2);
  c.result = KoboInputResult::OPENED;
  c.selected = base + "event2";
  ok1(discover(""));
  ok1(c.tried.back() == c.selected && c.tried.size() == 5);
  ok1(discover(nullptr, KoboInputRole::POWER));
  c.selected.clear();
  ok1(!discover());
  for (const char *name : {"event0", "event2", "event10", "eventual"})
    unlink((base + name).c_str());
  rmdir(directory);
  return exit_status();
}
