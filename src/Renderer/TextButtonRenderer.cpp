// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "TextButtonRenderer.hpp"
#include "ui/canvas/Canvas.hpp"
#include "Screen/Layout.hpp"
#include "Look/ButtonLook.hpp"

#include <algorithm>

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

#ifdef KOBO
  const bool inverted = state == ButtonState::FOCUSED ||
    state == ButtonState::PRESSED;
  const Color fallback_background = inverted ? COLOR_BLACK : COLOR_WHITE;
  const Color fallback_foreground = inverted ? COLOR_WHITE : COLOR_BLACK;
  const PixelRect text_rc{
    x, y,
    std::min(rc.right, x + int(text_size.width)),
    std::min(rc.bottom, y + int(text_size.height)),
  };

  canvas.DrawFilledRectangle(text_rc, fallback_background);
  canvas.SetTextColor(fallback_foreground);
#endif

  canvas.DrawClippedText({x, y}, rc.right - x, GetCaption());
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
