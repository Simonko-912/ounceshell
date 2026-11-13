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
 * Battery state for the status bar (scePower*) through libounce-nx's psm
 * client, and the overlay hook vita2d_common_dialog_update calls.
 *
 * psm has not been exercised on Switch 2 in this process role. If the
 * service or a query fails once, the battery is hidden for the rest of the
 * session instead of being asked every frame.
 */

#include <vitasdk.h>
#include <switch2_platform.h>

#include <switch.h>

#include <stdio.h>

#define BATTERY_REFRESH_MICROSECONDS (5ULL * 1000ULL * 1000ULL)
#define LOW_BATTERY_PERCENT 15

static int psm_available = 0;
static SceUInt64 battery_read_time = 0;
static Switch2BatteryState battery_state;

void switch2PowerInit(void) {
  Result rc = psmInitialize();
  if (R_FAILED(rc)) {
    fprintf(stderr, "power: psmInitialize: 0x%x; no battery display\n", rc);
    return;
  }

  psm_available = 1;
}

void switch2PowerExit(void) {
  if (psm_available) {
    psmExit();
    psm_available = 0;
  }
}

static int readBattery(void) {
  u32 percent = 0;
  Result rc = psmGetBatteryChargePercentage(&percent);
  if (R_FAILED(rc)) {
    fprintf(stderr, "power: psmGetBatteryChargePercentage: 0x%x; no battery display\n", rc);
    return -1;
  }

  PsmChargerType charger = PsmChargerType_Unconnected;
  if (R_FAILED(psmGetChargerType(&charger)))
    charger = PsmChargerType_Unconnected;

  battery_state.percent = percent > 100 ? 100 : (int)percent;
  battery_state.charging = charger != PsmChargerType_Unconnected;
  return 0;
}

int switch2GetBatteryState(Switch2BatteryState *state) {
  if (!psm_available)
    return -1;

  SceUInt64 now = sceKernelGetProcessTimeWide();
  if (battery_read_time == 0 || now - battery_read_time >= BATTERY_REFRESH_MICROSECONDS) {
    if (readBattery() != 0) {
      switch2PowerExit();
      return -1;
    }
    battery_read_time = now;
  }

  *state = battery_state;
  return 0;
}

int scePowerGetBatteryLifePercent(void) {
  Switch2BatteryState state;
  return switch2GetBatteryState(&state) == 0 ? state.percent : -1;
}

int scePowerIsBatteryCharging(void) {
  Switch2BatteryState state;
  return switch2GetBatteryState(&state) == 0 ? state.charging : 0;
}

int scePowerIsLowBattery(void) {
  Switch2BatteryState state;
  return switch2GetBatteryState(&state) == 0 && state.percent <= LOW_BATTERY_PERCENT;
}

int sceAppMgrAcquireBgmPort(void) {
  return 0;
}

int sceAppMgrReleaseBgmPort(void) {
  return 0;
}

int scePowerRequestDisplayOff(void) {
  return 0;
}

