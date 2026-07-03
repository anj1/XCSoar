// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TextButtonRenderer.hpp"
#include "ui/canvas/Canvas.hpp"
#include "Screen/Layout.hpp"
#include "Look/ButtonLook.hpp"

#include <algorithm>

#ifdef TARGET_IS_KOBO_NICKEL
static void
PrepareKoboNickelButtonCaption(Canvas &canvas, const PixelRect &rc,
                               ButtonState state,
                               const PixelPoint text_position,
                               const PixelSize text_size) noexcept
{
  const bool inverted = state == ButtonState::FOCUSED ||
    state == ButtonState::PRESSED;
  const Color fallback_background = inverted ? COLOR_BLACK : COLOR_WHITE;
  const Color fallback_foreground = inverted ? COLOR_WHITE : COLOR_BLACK;
  const PixelRect text_rc{
    text_position.x, text_position.y,
    std::min(rc.right, text_position.x + int(text_size.width)),
    std::min(rc.bottom, text_position.y + int(text_size.height)),
  };

  /*
   * KOBO_NICKEL currently loses button captions when transparent glyphs are
   * drawn directly over the dithered framebuffer canvas.  Keep the workaround
   * local to this target so it can be removed when the framebuffer text path is
   * fixed.
   */
  canvas.DrawFilledRectangle(text_rc, fallback_background);
  canvas.SetTextColor(fallback_foreground);
}
#endif

unsigned
TextButtonRenderer::GetMinimumButtonWidth(const ButtonLook &look,
                                          std::string_view caption) noexcept
{
  return 2 * (ButtonFrameRenderer::GetMargin() + Layout::GetTextPadding())
    + look.font->TextSize(caption).width;
}

inline void
TextButtonRenderer::DrawCaption(Canvas &canvas, const PixelRect &rc,
                                ButtonState state) const noexcept
{
  const ButtonLook &look = GetLook();

  canvas.SetBackgroundTransparent();

  switch (state) {
  case ButtonState::DISABLED:
    canvas.SetTextColor(look.disabled.color);
    break;

  case ButtonState::FOCUSED:
  case ButtonState::PRESSED:
    canvas.SetTextColor(look.focused.foreground_color);
    break;

  case ButtonState::SELECTED:
    canvas.SetTextColor(look.selected.foreground_color);
    break;

  case ButtonState::ENABLED:
    canvas.SetTextColor(look.standard.foreground_color);
    break;
  }

  canvas.Select(*look.font);

  const PixelSize text_size = canvas.CalcTextSize(GetCaption());
  const int x = rc.left + std::max(0, int(rc.GetWidth()) - int(text_size.width)) / 2;
  const int y = rc.top + std::max(0, int(rc.GetHeight()) - int(text_size.height)) / 2;
  const PixelPoint text_position{x, y};

#ifdef TARGET_IS_KOBO_NICKEL
  PrepareKoboNickelButtonCaption(canvas, rc, state, text_position, text_size);
#endif

  canvas.DrawClippedText(text_position, rc.right - x, GetCaption());
}

unsigned
TextButtonRenderer::GetMinimumButtonWidth() const noexcept
{
  return 2 * (frame_renderer.GetMargin() + Layout::GetTextPadding())
    + GetLook().font->TextSize(caption.c_str()).width;
}

void
TextButtonRenderer::DrawButton(Canvas &canvas, const PixelRect &rc,
                               ButtonState state) const noexcept
{
  frame_renderer.DrawButton(canvas, rc, state);

  if (!caption.empty())
    DrawCaption(canvas, frame_renderer.GetDrawingRect(rc, state),
                state);
}
