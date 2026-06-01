// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "ui/canvas/custom/TopCanvas.hpp"
#include "ui/canvas/Canvas.hpp"
#include "lib/fmt/SystemError.hxx"

#ifdef USE_FB
#include "ui/canvas/memory/Export.hpp"
#endif

#ifdef USE_FB
#include "Hardware/DisplayDPI.hpp"
#endif

#if defined(KOBO) && defined(USE_FB)
#include "Kobo/Model.hpp"
#include "mxcfb.h"
#endif

#if defined(KOBO) && defined(TARGET_IS_KOBO_NICKEL)
#include <fbink.h>
#endif

#include <algorithm>

#ifdef USE_FB
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <cassert>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static unsigned
TranslateDimension(unsigned value) noexcept
{
#ifdef KOBO
  if (value == 1024 && DetectKoboModel() == KoboModel::AURA)
    /* the Kobo Aura announces 1024 pixel rows, but the physical
       display only shows 1014 */
    value -= 10;
#endif

  return value;
}

static unsigned
GetWidth(const struct fb_var_screeninfo &vinfo) noexcept
{
  return TranslateDimension(vinfo.xres);
}

static unsigned
GetHeight(const struct fb_var_screeninfo &vinfo) noexcept
{
  return TranslateDimension(vinfo.yres);
}

static PixelSize
GetSize(const struct fb_var_screeninfo &vinfo) noexcept
{
  return PixelSize(GetWidth(vinfo), GetHeight(vinfo));
}

#endif
TopCanvas::~TopCanvas() noexcept
{
  buffer.Free();

#ifdef USE_FB
  if (fd >= 0) {
#ifdef TARGET_IS_KOBO_NICKEL
    fbink_close(fd);
#else
    close(fd);
#endif
    fd = -1;
  }
#endif
}
#ifdef USE_FB

TopCanvas::TopCanvas(UI::Display &_display)
  :display(_display)
{
  assert(fd < 0);

#ifdef TARGET_IS_KOBO_NICKEL
  fd = fbink_open();
  if (fd < 0)
    throw FmtErrno("Failed to fbink_open");

  memset(&fbink_cfg, 0, sizeof(FBInkConfig));
  fbink_cfg.is_quiet = true;
  fbink_init(fd, &fbink_cfg);
#else
  const char *path = "/dev/fb0";
  fd = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (fd < 0)
    throw FmtErrno("Failed to open {}", path);
#endif

  struct fb_fix_screeninfo finfo;
  if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0)
    throw MakeErrno("FBIOGET_FSCREENINFO failed");

  if (finfo.type != FB_TYPE_PACKED_PIXELS)
    throw std::runtime_error("Unsupported console hardware");

  switch (finfo.visual) {
  case FB_VISUAL_TRUECOLOR:
  case FB_VISUAL_PSEUDOCOLOR:
  case FB_VISUAL_STATIC_PSEUDOCOLOR:
  case FB_VISUAL_DIRECTCOLOR:
    break;

  default:
    throw std::runtime_error("Unsupported console hardware");
  }

#ifdef TARGET_IS_KOBO_NICKEL
  /* FBInk owns the MTK/Nickel frame buffer mapping. */
  map = nullptr;
#else
  /* Memory map the device, compensating for buggy PPC mmap() */
  const off_t page_size = getpagesize();
  off_t offset = off_t(finfo.smem_start)
    - (off_t(finfo.smem_start) &~ (page_size - 1));
  off_t map_size = finfo.smem_len + offset;
  map = mmap(nullptr, map_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == (void *)-1)
    throw MakeErrno("Unable to memory map the video hardware");
#endif

  /* Determine the current screen depth */
  struct fb_var_screeninfo vinfo;
  if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0)
    throw MakeErrno("Couldn't get console pixel format");

#ifdef GREYSCALE
  /* switch the frame buffer to 8 bits per pixel greyscale */

#ifndef TARGET_IS_KOBO_NICKEL
  vinfo.bits_per_pixel = 8;
  vinfo.grayscale = true;

  if (ioctl(fd, FBIOPUT_VSCREENINFO, &vinfo) < 0)
    throw MakeErrno("Couldn't set greyscale pixel format");

  /* read new finfo */
  ioctl(fd, FBIOGET_FSCREENINFO, &finfo);
  ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
#endif

  map_bpp = vinfo.bits_per_pixel / 8;
  if (map_bpp != 1 && map_bpp != 2 && map_bpp != 4)
    throw std::runtime_error("Unsupported console hardware");
#else
  map_bpp = vinfo.bits_per_pixel / 8;
  if (map_bpp != 2 && map_bpp != 4)
    throw std::runtime_error("Unsupported console hardware");
#endif

#ifdef TARGET_IS_KOBO_NICKEL
  FBInkState fbink_state;
  fbink_get_state(&fbink_cfg, &fbink_state);

  map_pitch = fbink_state.scanline_stride != 0
    ? fbink_state.scanline_stride
    : finfo.line_length;
  map_bpp = fbink_state.bpp != 0
    ? fbink_state.bpp / 8
    : vinfo.bits_per_pixel / 8;

#ifdef GREYSCALE
  if (map_bpp != 1 && map_bpp != 2 && map_bpp != 4)
    throw std::runtime_error("Unsupported console hardware");
#else
  if (map_bpp != 2 && map_bpp != 4)
    throw std::runtime_error("Unsupported console hardware");
#endif
#else
  map_pitch = finfo.line_length;
#endif
  epd_update_marker = 0;

#ifdef KOBO
#ifndef TARGET_IS_KOBO_NICKEL
  ioctl(fd, MXCFB_SET_UPDATE_SCHEME, UPDATE_SCHEME_QUEUE_AND_MERGE);
#endif

  switch(DetectKoboModel()) {
  case KoboModel::UNKNOWN:
  case KoboModel::MINI:
  case KoboModel::TOUCH:
  case KoboModel::GLO:
  case KoboModel::AURA:
  case KoboModel::NIA:
    frame_sync = false;
    break;

  case KoboModel::TOUCH2:
  case KoboModel::GLO_HD:
  case KoboModel::AURA2:
  case KoboModel::CLARA_HD:
  case KoboModel::CLARA_2E:
  case KoboModel::CLARA_COLOUR:
  case KoboModel::LIBRA2:
  case KoboModel::LIBRA_H2O:
    frame_sync = true;
    break;

  };
#endif

#ifdef TARGET_IS_KOBO_NICKEL
  const auto new_size = fbink_state.screen_width != 0 &&
      fbink_state.screen_height != 0
    ? PixelSize(fbink_state.screen_width, fbink_state.screen_height)
    : ::GetSize(vinfo);
#else
  const auto new_size = ::GetSize(vinfo);
#endif

  if (vinfo.width > 0 && vinfo.height > 0)
    Display::ProvideSizeMM(new_size.width, new_size.height,
                           vinfo.width, vinfo.height);

  buffer.Allocate(new_size);
}

inline PixelSize
TopCanvas::GetNativeSize() const noexcept
{
#ifdef TARGET_IS_KOBO_NICKEL
  FBInkState state;
  fbink_get_state(&fbink_cfg, &state);
  if (state.screen_width != 0 && state.screen_height != 0)
    return PixelSize(state.screen_width, state.screen_height);
#endif

  struct fb_var_screeninfo vinfo;
  ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
  return ::GetSize(vinfo);
}

bool
TopCanvas::CheckResize() noexcept
{
  return CheckResize(GetNativeSize());
}

#elif defined(USE_VFB)

TopCanvas::TopCanvas(UI::Display &_display, PixelSize new_size)
  :display(_display)
{
  buffer.Allocate(new_size);

  // suppress -Wunused
  (void)display;
}

#else
#error No implementation
#endif

bool
TopCanvas::CheckResize(const PixelSize new_native_size) noexcept
{
  const PixelSize new_size = new_native_size;
  if (new_size == GetSize())
    /* no change */
    return false;

  /* changed: update the size and allocate a new buffer */

#ifdef USE_FB
#ifdef TARGET_IS_KOBO_NICKEL
  FBInkState state;
  fbink_get_state(&fbink_cfg, &state);

  if (state.scanline_stride != 0)
    map_pitch = state.scanline_stride;
  if (state.bpp != 0)
    map_bpp = state.bpp / 8;
#else
  struct fb_fix_screeninfo finfo;
  ioctl(fd, FBIOGET_FSCREENINFO, &finfo);

  map_pitch = finfo.line_length;
#endif
#endif

  buffer.Free();
  buffer.Allocate(new_size);
  return true;
}

Canvas
TopCanvas::Lock()
{
  return Canvas(buffer);
}

void
TopCanvas::Unlock() noexcept
{
}

void
TopCanvas::Flip()
{
#ifdef USE_FB

#ifdef TARGET_IS_KOBO_NICKEL

  [[maybe_unused]] const int reinit_result = fbink_reinit(fd, &fbink_cfg);

  FBInkState state;
  fbink_get_state(&fbink_cfg, &state);

  const unsigned fb_pitch = state.scanline_stride != 0
    ? state.scanline_stride
    : map_pitch;
  const unsigned fb_bpp = state.bpp != 0
    ? state.bpp / 8
    : map_bpp;

  size_t fb_size = 0;
  unsigned char *fbp = fbink_get_fb_pointer(fd, &fb_size);

  if (fbp != nullptr && buffer.data != nullptr &&
      (fb_bpp == 1 || fb_bpp == 2 || fb_bpp == 4) &&
      fb_pitch >= buffer.size.width * fb_bpp &&
      fb_size >= static_cast<size_t>(fb_pitch) * buffer.size.height) {
#ifdef GREYSCALE
    CopyFromGreyscale(
#ifdef DITHER
                      dither,
#endif
#ifdef KOBO
                      enable_dither,
#endif
                      fbp, fb_pitch, fb_bpp,
                      buffer);
#else
    if (fb_bpp == 2 || fb_bpp == 4)
      CopyFromBGRA(fbp, fb_pitch, fb_bpp, buffer);
#endif
  }

  FBInkConfig cfg = fbink_cfg;
  cfg.is_quiet = true;
  cfg.no_refresh = false;

  fbink_refresh(fd, 0, 0, buffer.size.width, buffer.size.height, &cfg);
  return;

#else /* !TARGET_IS_KOBO_NICKEL */

#ifdef GREYSCALE
  CopyFromGreyscale(
#ifdef DITHER
                    dither,
#endif
#ifdef KOBO
                    enable_dither,
#endif
                    map, map_pitch, map_bpp,
                    buffer);
#else
  CopyFromBGRA(map, map_pitch, map_bpp, buffer);
#endif

#ifdef KOBO
  if (frame_sync)
    Wait();

  epd_update_marker++;

  KoboModel kobo_model = DetectKoboModel();
  struct mxcfb_update_data epd_update_data = {
    {
      0, 0, buffer.size.width, buffer.size.height
    },

    uint32_t(enable_dither &&
             (kobo_model == KoboModel::TOUCH2 ||
              kobo_model == KoboModel::GLO_HD ||
              kobo_model == KoboModel::AURA2 ||
              kobo_model == KoboModel::LIBRA2 ||
              kobo_model == KoboModel::LIBRA_H2O ||
              kobo_model == KoboModel::CLARA_HD ||
              kobo_model == KoboModel::CLARA_2E ||
              kobo_model == KoboModel::CLARA_COLOUR)
             ? WAVEFORM_MODE_A2
             : WAVEFORM_MODE_AUTO),
    UPDATE_MODE_FULL,
    epd_update_marker,
    TEMP_USE_AMBIENT,
    enable_dither ? EPDC_FLAG_FORCE_MONOCHROME : 0,
  };

  ioctl(fd, MXCFB_SEND_UPDATE, &epd_update_data);
#endif /* KOBO */

#endif /* TARGET_IS_KOBO_NICKEL */

#endif /* USE_FB */
}

#ifdef KOBO

void
TopCanvas::Wait() noexcept
{
  ioctl(fd, MXCFB_WAIT_FOR_UPDATE_COMPLETE, &epd_update_marker);
}

#endif
