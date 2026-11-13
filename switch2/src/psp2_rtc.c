/*
  VitaShell for Switch 2
  Copyright (C) 2026, VitaShell Switch 2 port contributors

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * sceRtc date arithmetic. A tick is a count of microseconds since
 * 0001-01-01 00:00:00 in the proleptic Gregorian calendar, as on the Vita.
 * The wall clock comes from newlib's time(), which libounce-nx bases on the
 * Switch time service; the local time zone is newlib's TZ, which
 * libounce-nx sets from the device's time zone rule at startup.
 */

#include <vitasdk.h>

#include <string.h>
#include <time.h>

#define MICROSECONDS_PER_SECOND 1000000ULL
#define SECONDS_PER_DAY 86400LL

/* Days from 1970-01-01 to the given civil date (Howard Hinnant's algorithm). */
static int64_t daysFromCivil(int64_t year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned year_of_era = (unsigned)(year - era * 400);
  const unsigned day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + (int64_t)day_of_era - 719468;
}

/* The civil date of a count of days since 1970-01-01. */
static void civilFromDays(int64_t days, int64_t *year, unsigned *month, unsigned *day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned day_of_era = (unsigned)(days - era * 146097);
  const unsigned year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
  const unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const unsigned month_index = (5 * day_of_year + 2) / 153;
  *day = day_of_year - (153 * month_index + 2) / 5 + 1;
  *month = month_index < 10 ? month_index + 3 : month_index - 9;
  *year = (int64_t)year_of_era + era * 400 + (*month <= 2);
}

/* Days from 0001-01-01 to 1970-01-01. */
static int64_t epochDaysFromYearOne(void) {
  return -daysFromCivil(1, 1, 1);
}

static int64_t secondsSinceEpoch(const SceDateTime *time) {
  int64_t days = daysFromCivil(time->year, time->month, time->day);
  return days * SECONDS_PER_DAY + time->hour * 3600LL + time->minute * 60LL + time->second;
}

static void dateTimeFromSeconds(SceDateTime *time, int64_t seconds, unsigned int microsecond) {
  int64_t days = seconds / SECONDS_PER_DAY;
  int64_t remainder = seconds % SECONDS_PER_DAY;
  if (remainder < 0) {
    remainder += SECONDS_PER_DAY;
    days--;
  }

  int64_t year;
  unsigned month, day;
  civilFromDays(days, &year, &month, &day);

  time->year = (unsigned short)year;
  time->month = (unsigned short)month;
  time->day = (unsigned short)day;
  time->hour = (unsigned short)(remainder / 3600);
  time->minute = (unsigned short)((remainder % 3600) / 60);
  time->second = (unsigned short)(remainder % 60);
  time->microsecond = microsecond;
}

int sceRtcGetTick(const SceDateTime *time, SceRtcTick *tick) {
  if (!time || !tick)
    return -1;

  int64_t seconds = secondsSinceEpoch(time) + epochDaysFromYearOne() * SECONDS_PER_DAY;
  if (seconds < 0)
    seconds = 0;

  tick->tick = (SceUInt64)seconds * MICROSECONDS_PER_SECOND + time->microsecond;
  return 0;
}

int sceRtcSetTick(SceDateTime *time, const SceRtcTick *tick) {
  if (!time || !tick)
    return -1;

  int64_t seconds = (int64_t)(tick->tick / MICROSECONDS_PER_SECOND) -
                    epochDaysFromYearOne() * SECONDS_PER_DAY;
  dateTimeFromSeconds(time, seconds, (unsigned int)(tick->tick % MICROSECONDS_PER_SECOND));
  return 0;
}

int sceRtcSetTime_t(SceDateTime *time, time_t value) {
  if (!time)
    return -1;

  dateTimeFromSeconds(time, (int64_t)value, 0);
  return 0;
}

int sceRtcGetCurrentClock(SceDateTime *date_time, int time_zone) {
  if (!date_time)
    return -1;

  time_t now = 0;
  if (time(&now) == (time_t)-1)
    now = 0;

  dateTimeFromSeconds(date_time, (int64_t)now + (int64_t)time_zone * 60, 0);
  return 0;
}

/* The local time zone's offset from UTC at a given instant, in seconds. */
static int64_t localOffsetSeconds(int64_t utc_seconds) {
  time_t value = (time_t)utc_seconds;
  struct tm local;
  if (!localtime_r(&value, &local))
    return 0;

  SceDateTime local_time;
  local_time.year = (unsigned short)(local.tm_year + 1900);
  local_time.month = (unsigned short)(local.tm_mon + 1);
  local_time.day = (unsigned short)local.tm_mday;
  local_time.hour = (unsigned short)local.tm_hour;
  local_time.minute = (unsigned short)local.tm_min;
  local_time.second = (unsigned short)local.tm_sec;
  local_time.microsecond = 0;

  return secondsSinceEpoch(&local_time) - utc_seconds;
}

int sceRtcGetCurrentClockLocalTime(SceDateTime *date_time) {
  time_t now = 0;
  if (time(&now) == (time_t)-1)
    now = 0;

  int64_t offset_minutes = localOffsetSeconds((int64_t)now) / 60;
  return sceRtcGetCurrentClock(date_time, (int)offset_minutes);
}

static int64_t tickToEpochSeconds(const SceRtcTick *tick) {
  return (int64_t)(tick->tick / MICROSECONDS_PER_SECOND) - epochDaysFromYearOne() * SECONDS_PER_DAY;
}

static SceUInt64 addSeconds(SceUInt64 tick, int64_t seconds) {
  int64_t microseconds = seconds * (int64_t)MICROSECONDS_PER_SECOND;
  if (microseconds < 0 && (SceUInt64)(-microseconds) > tick)
    return 0;
  return tick + (SceUInt64)microseconds;
}

int sceRtcConvertUtcToLocalTime(const SceRtcTick *utc, SceRtcTick *local_time) {
  if (!utc || !local_time)
    return -1;

  int64_t offset = localOffsetSeconds(tickToEpochSeconds(utc));
  local_time->tick = addSeconds(utc->tick, offset);
  return 0;
}

int sceRtcConvertLocalTimeToUtc(const SceRtcTick *local_time, SceRtcTick *utc) {
  if (!local_time || !utc)
    return -1;

  // The offset at the local instant read as UTC; exact except within an
  // hour of a daylight saving change.
  int64_t offset = localOffsetSeconds(tickToEpochSeconds(local_time));
  utc->tick = addSeconds(local_time->tick, -offset);
  return 0;
}
