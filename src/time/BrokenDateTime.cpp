// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "BrokenDateTime.hpp"
#include "Calendar.hxx"
#include "Convert.hxx"

#include <cassert>

#include <chrono>
#include <cstdint>
#include <time.h>

static int64_t
FloorDiv(int64_t numerator, int64_t denominator) noexcept
{
  int64_t quotient = numerator / denominator;
  const int64_t remainder = numerator % denominator;
  if (remainder < 0)
    --quotient;

  return quotient;
}

static BrokenDateTime
ToBrokenDateTimeUtcSeconds(int64_t t) noexcept
{
  /* Build UTC civil time from a signed second count, without converting through
   * std::time_t (so values beyond 32-bit time_t on ILP32 are not truncated). */
  const int64_t days = FloorDiv(t, 86400);
  const unsigned seconds_of_day = t - days * 86400;

  /* Howard Hinnant's civil-from-days algorithm, where day zero is
   * 1970-01-01. */
  int64_t z = days + 719468;
  const int64_t era = FloorDiv(z, 146097);
  const unsigned doe = z - era * 146097;
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;

  BrokenDateTime dt;
  dt.day = doy - (153 * mp + 2) / 5 + 1;
  dt.month = mp < 10 ? mp + 3 : mp - 9;
  dt.year = y + (dt.month <= 2);
  dt.day_of_week = static_cast<int8_t>((days + 4) % 7);
  if (dt.day_of_week < 0)
    dt.day_of_week += 7;
  dt.hour = seconds_of_day / 3600;
  dt.minute = (seconds_of_day / 60) % 60;
  dt.second = seconds_of_day % 60;
  return dt;
}

static const BrokenDateTime
ToBrokenDateTime(const struct tm &tm) noexcept
{
  BrokenDateTime dt;

  dt.year = tm.tm_year + 1900;
  dt.month = tm.tm_mon + 1;
  dt.day = tm.tm_mday;
  dt.day_of_week = tm.tm_wday;

  dt.hour = tm.tm_hour;
  dt.minute = tm.tm_min;
  dt.second = tm.tm_sec;

  return dt;
}

BrokenDateTime::BrokenDateTime(std::chrono::system_clock::time_point tp) noexcept
  : BrokenDateTime(FromUnixTime(
        (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
            tp.time_since_epoch())
            .count())) {
}

std::chrono::system_clock::time_point
BrokenDateTime::ToTimePoint() const noexcept
{
  assert(IsPlausible());

  struct tm tm;
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
  tm.tm_isdst = 0;
  tm.tm_wday = day_of_week;
  tm.tm_yday = -1;

  return TimeGm(tm);
}

const BrokenDateTime
BrokenDateTime::NowUTC() noexcept
{
  return BrokenDateTime{std::chrono::system_clock::now()};
}

const BrokenDateTime
BrokenDateTime::NowLocal() noexcept
{
  return ToBrokenDateTime(LocalTime(std::chrono::system_clock::now()));
}

BrokenDateTime
BrokenDateTime::ToLocal() const noexcept
{
  assert(IsPlausible());

  return ToBrokenDateTime(LocalTime(ToTimePoint()));
}

BrokenDateTime
BrokenDateTime::FromUnixTime(int64_t t) noexcept
{
  return ToBrokenDateTimeUtcSeconds(t);
}
