// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Kobo/Model.hpp"
#include "DisplaySettings.hpp"
#include "Asset.hpp"
#include "TestUtil.hpp"

#include <span>
#include <string_view>
#include <array>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>

static size_t read_limit = 512;
static unsigned read_interrupts = 0;

extern "C" ssize_t __real_pread(int fd, void *buffer, size_t size, off_t offset);
extern "C" ssize_t __wrap_pread(int fd, void *buffer, size_t size, off_t offset);

extern "C" ssize_t
__wrap_pread(int fd, void *buffer, size_t size, off_t offset)
{
  if (read_interrupts > 0) {
    --read_interrupts;
    errno = EINTR;
    return -1;
  }
  return __real_pread(fd, buffer, std::min(size, read_limit), offset);
}

class ModelFile {
  char path[32] = "/tmp/xcsoar-model-XXXXXX";
  int fd = mkstemp(path);

public:
  ModelFile() { if (fd < 0) abort(); }
  ~ModelFile() { close(fd); unlink(path); }
  const char *Path() const { return path; }

  void Write(off_t offset, std::string_view serial, size_t size) {
    std::array<char, 512> data{};
    assert(serial.size() <= size && size <= data.size());
    std::copy(serial.begin(), serial.end(), data.begin());
    if (ftruncate(fd, 0) != 0 ||
        pwrite(fd, data.data(), size, offset) != ssize_t(size))
      abort();
  }
};

static void
TestSources()
{
  ModelFile hwcfg, legacy;
  legacy.Write(0x200, "SN-N365", 16);
  hwcfg.Write(0x800, "SN-N367", 512);
  ok1(DetectKoboModel(hwcfg.Path(), legacy.Path()) == KoboModel::CLARA_COLOUR);
  ok1(DetectKoboModel(hwcfg.Path(), "/nonexistent/xcsoar-legacy") ==
      KoboModel::CLARA_COLOUR);
  hwcfg.Write(0, "SN-N367", 512);
  ok1(DetectKoboModel(hwcfg.Path(), legacy.Path()) == KoboModel::CLARA_BW);
  hwcfg.Write(0x800, "SN-N999", 512);
  ok1(DetectKoboModel(hwcfg.Path(), legacy.Path()) == KoboModel::CLARA_BW);
  ok1(DetectKoboModel("/nonexistent/xcsoar-hwcfg", legacy.Path()) ==
      KoboModel::CLARA_BW);
  hwcfg.Write(0x800, "SN-N367", 7);
  ok1(DetectKoboModel(hwcfg.Path(), legacy.Path()) == KoboModel::CLARA_BW);
  legacy.Write(0x200, "SN-N365", 7);
  ok1(DetectKoboModel(hwcfg.Path(), legacy.Path()) == KoboModel::UNKNOWN);
  hwcfg.Write(0x800, "SN-N367", 512);
  read_limit = 3;
  read_interrupts = 2;
  ok1(DetectKoboModel(hwcfg.Path(), legacy.Path()) == KoboModel::CLARA_COLOUR);
  ok1(read_interrupts == 0);
  read_limit = 512;
  hwcfg.Write(0x800, "SN-P365", 512);
  ok1(DetectKoboModel(hwcfg.Path(), legacy.Path()) == KoboModel::CLARA_BW);
  hwcfg.Write(0x800, "SN-N999", 512);
  legacy.Write(0x200, "SN-N999", 16);
  ok1(DetectKoboModel(hwcfg.Path(), legacy.Path()) == KoboModel::UNKNOWN);
}

static KoboModel
Parse(std::string_view value) noexcept
{
  return ParseKoboModel(std::span{value.data(), value.size()});
}

int
main()
{
  plan_tests(33);

  TestSources();

  ok1(Parse("SN-N365") == KoboModel::CLARA_BW);
  ok1(Parse("SN-P365") == KoboModel::CLARA_BW);
  ok1(Parse("SN-N367") == KoboModel::CLARA_COLOUR);
  ok1(Parse("hwcfg-data-SN-N367-more-data") == KoboModel::CLARA_COLOUR);
  ok1(Parse("SN-N999") == KoboModel::UNKNOWN);
  ok1(Parse("SN-N36") == KoboModel::UNKNOWN);

  ok1(IsKoboMediaTek(KoboModel::CLARA_BW));
  ok1(IsKoboMediaTek(KoboModel::CLARA_COLOUR));
  ok1(!IsKoboMediaTek(KoboModel::CLARA_2E));

  ok1(std::string_view{GetKoboWifiInterface(KoboModel::CLARA_BW)} == "wlan0");
  ok1(std::string_view{GetKoboWifiInterface(KoboModel::CLARA_COLOUR)} == "wlan0");
  ok1(std::string_view{GetKoboWifiInterface(KoboModel::CLARA_2E)} == "mlan0");
  ok1(std::string_view{GetKoboWifiInterface(KoboModel::GLO_HD)} == "eth0");

  ok1(GetKoboDefaultDisplayType(KoboModel::CLARA_BW) == DisplayType::E_INK);
  ok1(GetKoboDefaultDisplayType(KoboModel::CLARA_COLOUR) ==
      DisplayType::COLOR_E_INK);
  ok1(GetKoboDefaultDisplayType(KoboModel::UNKNOWN) == DisplayType::E_INK);

  SetDisplayType(DisplayType::E_INK);
  ok1(!HasColors());
  ok1(HasEPaper());
  SetDisplayType(DisplayType::COLOR_E_INK);
  ok1(HasColors());
  ok1(HasEPaper());
  SetDisplayType(DisplayType::LCD);
  ok1(HasColors());
  ok1(!HasEPaper());

  return exit_status();
}
