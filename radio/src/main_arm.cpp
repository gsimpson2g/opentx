/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "opentx.h"

uint8_t currentSpeakerVolume = 255;
uint8_t requiredSpeakerVolume = 255;
uint8_t mainRequestFlags = 0;

void handleUsbConnection()
{
#if defined(STM32) && !defined(SIMU)
  static bool usbStarted = false;

  if (!usbStarted && usbPlugged()) {
    usbStarted = true;
    usbStart();
#if defined(USB_MASS_STORAGE)
    opentxClose(false);
    usbPluggedIn();
#endif
  }
  if (usbStarted && !usbPlugged()) {
    usbStarted = false;
    usbStop();
#if defined(USB_MASS_STORAGE) && !defined(EEPROM)
    opentxResume();
#endif
  }

#if defined(USB_JOYSTICK)
  if (usbStarted ) {
    usbJoystickUpdate();
  }
#endif

#endif // defined(STM32) && !defined(SIMU)
}

void checkSpeakerVolume()
{
  if (currentSpeakerVolume != requiredSpeakerVolume) {
    currentSpeakerVolume = requiredSpeakerVolume;
#if !defined(SOFTWARE_VOLUME)
    setScaledVolume(currentSpeakerVolume);
#endif
  }
}

#if defined(EEPROM)
void checkEeprom()
{
  if (!usbPlugged()) {
    if (eepromIsWriting())
      eepromWriteProcess();
    else if (TIME_TO_WRITE())
      storageCheck(false);
  }
}
#else
void checkEeprom()
{
#if defined(RAMBACKUP)
  if (TIME_TO_RAMBACKUP()) {
    rambackupWrite();
    rambackupDirtyMsk = 0;
  }
#endif
  if (TIME_TO_WRITE()) {
    storageCheck(false);
  }
}
#endif

#define BAT_AVG_SAMPLES       8

void checkBatteryAlarms()
{
  // TRACE("checkBatteryAlarms()");
  if (IS_TXBATT_WARNING() && g_vbat100mV>50) {
    AUDIO_TX_BATTERY_LOW();
    // TRACE("checkBatteryAlarms(): battery low");
  }
#if defined(PCBSKY9X)
  else if (g_eeGeneral.temperatureWarn && getTemperature() >= g_eeGeneral.temperatureWarn) {
    AUDIO_TX_TEMP_HIGH();
  }
  else if (g_eeGeneral.mAhWarn && (g_eeGeneral.mAhUsed + Current_used * (488 + g_eeGeneral.txCurrentCalibration)/8192/36) / 500 >= g_eeGeneral.mAhWarn) { // TODO move calculation into board file
    AUDIO_TX_MAH_HIGH();
  }
#endif
}

void checkBattery()
{
  static uint32_t batSum;
  static uint8_t sampleCount;
  // filter battery voltage by averaging it
  if (g_vbat100mV == 0) {
    g_vbat100mV = (getBatteryVoltage() + 5) / 10;
    batSum = 0;
    sampleCount = 0;
  }
  else {
    batSum += getBatteryVoltage();
    // TRACE("checkBattery(): sampled = %d", getBatteryVoltage());
    if (++sampleCount >= BAT_AVG_SAMPLES) {
      g_vbat100mV = (batSum + BAT_AVG_SAMPLES * 5 ) / (BAT_AVG_SAMPLES * 10);
      batSum = 0;
      sampleCount = 0;
      // TRACE("checkBattery(): g_vbat100mV = %d", g_vbat100mV);
    }
  }
}

void periodicTick_1s()
{
  checkBattery();
}

void periodicTick_10s()
{
  checkBatteryAlarms();
}

void periodicTick()
{
  static uint8_t count10s;
  static uint32_t lastTime;
  if ( (get_tmr10ms() - lastTime) >= 100 ) {
    lastTime += 100;
    periodicTick_1s();
    if (++count10s >= 10) {
      count10s = 0;
      periodicTick_10s();
    }
  }
}

#if defined(GUI) && defined(COLORLCD)
void guiMain(event_t evt)
{
  bool refreshNeeded = false;

#if defined(LUA)
  uint32_t t0 = get_tmr10ms();
  static uint32_t lastLuaTime = 0;
  uint16_t interval = (lastLuaTime == 0 ? 0 : (t0 - lastLuaTime));
  lastLuaTime = t0;
  if (interval > maxLuaInterval) {
    maxLuaInterval = interval;
  }

  // run Lua scripts that don't use LCD (to use CPU time while LCD DMA is running)
  DEBUG_TIMER_START(debugTimerLuaBg);
  luaTask(0, RUN_MIX_SCRIPT | RUN_FUNC_SCRIPT | RUN_TELEM_BG_SCRIPT, false);
  DEBUG_TIMER_STOP(debugTimerLuaBg);
  // wait for LCD DMA to finish before continuing, because code from this point
  // is allowed to change the contents of LCD buffer
  //
  // WARNING: make sure no code above this line does any change to the LCD display buffer!
  //
  DEBUG_TIMER_START(debugTimerLcdRefreshWait);
  lcdRefreshWait();
  DEBUG_TIMER_STOP(debugTimerLcdRefreshWait);

  // draw LCD from menus or from Lua script
  // run Lua scripts that use LCD

  DEBUG_TIMER_START(debugTimerLuaFg);
  refreshNeeded = luaTask(evt, RUN_STNDAL_SCRIPT, true);
  if (!refreshNeeded) {
    refreshNeeded = luaTask(evt, RUN_TELEM_FG_SCRIPT, true);
  }
  DEBUG_TIMER_STOP(debugTimerLuaFg);

  t0 = get_tmr10ms() - t0;
  if (t0 > maxLuaDuration) {
    maxLuaDuration = t0;
  }
#else
  lcdRefreshWait();   // WARNING: make sure no code above this line does any change to the LCD display buffer!
#endif

  if (!refreshNeeded) {
    DEBUG_TIMER_START(debugTimerMenus);
    while (1) {
      // normal GUI from menus
      const char * warn = warningText;
      uint8_t menu = popupMenuNoItems;

      static bool popupDisplayed = false;
      if (warn || menu) {
        if (popupDisplayed == false) {
          menuHandlers[menuLevel](EVT_REFRESH);
          lcdDrawBlackOverlay();
          TIME_MEASURE_START(storebackup);
          lcdStoreBackupBuffer();
          TIME_MEASURE_STOP(storebackup);
        }
        if (popupDisplayed == false || evt) {
          popupDisplayed = lcdRestoreBackupBuffer();
          if (warn) DISPLAY_WARNING(evt);
          if (menu) {
            const char * result = runPopupMenu(evt);
            if (result) {
              popupMenuHandler(result);
              if (menuEvent == 0) {
                evt = EVT_REFRESH;
                continue;
              }
            }
          }
          refreshNeeded = true;
        }
      }
      else {
        if (popupDisplayed) {
          if (evt == 0) {
            evt = EVT_REFRESH;
          }
          popupDisplayed = false;
        }
        DEBUG_TIMER_START(debugTimerMenuHandlers);
        refreshNeeded = menuHandlers[menuLevel](evt);
        DEBUG_TIMER_STOP(debugTimerMenuHandlers);
      }

      if (menuEvent == EVT_ENTRY) {
        menuVerticalPosition = 0;
        menuHorizontalPosition = 0;
        evt = menuEvent;
        menuEvent = 0;
      }
      else if (menuEvent == EVT_ENTRY_UP) {
        menuVerticalPosition = menuVerticalPositions[menuLevel];
        menuHorizontalPosition = 0;
        evt = menuEvent;
        menuEvent = 0;
      }
      else {
        break;
      }
    }
    DEBUG_TIMER_STOP(debugTimerMenus);
  }

  if (refreshNeeded) {
    DEBUG_TIMER_START(debugTimerLcdRefresh);
    lcdRefresh();
    DEBUG_TIMER_STOP(debugTimerLcdRefresh);
  }
}
#elif defined(GUI)

void handleGui(event_t event) {
  // if Lua standalone, run it and don't clear the screen (Lua will do it)
  // else if Lua telemetry view, run it and don't clear the screen
  // else clear scren and show normal menus
#if defined(LUA)
  if (luaTask(event, RUN_STNDAL_SCRIPT, true)) {
    // standalone script is active
  }
  else if (luaTask(event, RUN_TELEM_FG_SCRIPT, true)) {
    // the telemetry screen is active
    // prevent events from keys MENU, UP, DOWN, ENT(short) and EXIT(short) from reaching the normal menus,
    // so Lua telemetry script can fully use them
    if (event) {
      uint8_t key = EVT_KEY_MASK(event);
      // no need to filter out MENU and ENT(short), because they are not used by menuViewTelemetryFrsky()
      if (key == KEY_PLUS || key == KEY_MINUS || (!IS_KEY_LONG(event) && key == KEY_EXIT)) {
        // TRACE("Telemetry script event 0x%02x killed", event);
        event = 0;
      }
    }
    menuHandlers[menuLevel](event);
    // todo     drawStatusLine(); here???
  }
  else
#endif
  {
    lcdClear();
    menuHandlers[menuLevel](event);
    drawStatusLine();
  }
}

bool inPopupMenu = false;

void guiMain(event_t evt)
{
#if defined(LUA)
  // TODO better lua stopwatch
  uint32_t t0 = get_tmr10ms();
  static uint32_t lastLuaTime = 0;
  uint16_t interval = (lastLuaTime == 0 ? 0 : (t0 - lastLuaTime));
  lastLuaTime = t0;
  if (interval > maxLuaInterval) {
    maxLuaInterval = interval;
  }

  // run Lua scripts that don't use LCD (to use CPU time while LCD DMA is running)
  luaTask(0, RUN_MIX_SCRIPT | RUN_FUNC_SCRIPT | RUN_TELEM_BG_SCRIPT, false);

  t0 = get_tmr10ms() - t0;
  if (t0 > maxLuaDuration) {
    maxLuaDuration = t0;
  }
#endif //#if defined(LUA)

  // wait for LCD DMA to finish before continuing, because code from this point
  // is allowed to change the contents of LCD buffer
  //
  // WARNING: make sure no code above this line does any change to the LCD display buffer!
  //
  lcdRefreshWait();

  if (menuEvent) {
    // we have a popupMenuActive entry or exit event
    menuVerticalPosition = (menuEvent == EVT_ENTRY_UP) ? menuVerticalPositions[menuLevel] : 0;
    menuHorizontalPosition = 0;
    evt = menuEvent;
    menuEvent = 0;
  }

  if (warningText) {
    // show warning on top of the normal menus
    handleGui(0); // suppress events, they are handled by the warning
    DISPLAY_WARNING(evt);
  }
  else if (popupMenuNoItems > 0) {
    // popup menu is active display it on top of normal menus
    handleGui(0); // suppress events, they are handled by the popup
    if (!inPopupMenu) {
      TRACE("Popup Menu started");
      inPopupMenu = true;
    }
    const char * result = runPopupMenu(evt);
    if (result) {
      TRACE("popupMenuHandler(%s)", result);
      popupMenuHandler(result);
    }
  }
  else {
    // normal menus
    if (inPopupMenu) {
      TRACE("Popup Menu ended");
      inPopupMenu = false;
    }
    handleGui(evt);
  }

  lcdRefresh();
}
#endif

void perMain()
{
  DEBUG_TIMER_START(debugTimerPerMain1);
#if defined(PCBSKY9X) && !defined(REVA)
  calcConsumption();
#endif
  checkSpeakerVolume();
  checkEeprom();
  logsWrite();
  handleUsbConnection();
  checkTrainerSettings();
  periodicTick();
  DEBUG_TIMER_STOP(debugTimerPerMain1);

  if (mainRequestFlags & (1 << REQUEST_FLIGHT_RESET)) {
    TRACE("Executing requested Flight Reset");
    flightReset();
    mainRequestFlags &= ~(1 << REQUEST_FLIGHT_RESET);
  }

  event_t evt = getEvent(false);
  if (evt && (g_eeGeneral.backlightMode & e_backlight_mode_keys)) {
    // on keypress turn the light on
    backlightOn();
  }
  doLoopCommonActions();
#if defined(NAVIGATION_STICKS)
  uint8_t sticks_evt = getSticksNavigationEvent();
  if (sticks_evt) {
    evt = sticks_evt;
  }
#endif

#if defined(RAMBACKUP)
  if (unexpectedShutdown) {
    drawFatalErrorScreen(STR_EMERGENCY_MODE);
    return;
  }
#endif
  
#if defined(PCBHORUS)
  // TODO if it is OK on HORUS it could be ported to all other boards
  // But in this case it's needed to define sdMount for all boards, because sdInit also initializes the SD mutex
  static uint32_t sdcard_present_before = SD_CARD_PRESENT();
  uint32_t sdcard_present_now = SD_CARD_PRESENT();
  if (sdcard_present_now && !sdcard_present_before) {
    sdMount();
  }
  sdcard_present_before = sdcard_present_now;
#endif

#if !defined(EEPROM)
  // In case the SD card is removed during the session
  if (!SD_CARD_PRESENT()) {
    drawFatalErrorScreen(STR_NO_SDCARD);
    return;
  }
#endif
    
#if defined(USB_MASS_STORAGE)
  if (usbPlugged()) {
    // disable access to menus
    lcdClear();
    menuMainView(0);
    lcdRefresh();
    return;
  }
#endif

#if defined(GUI)
  DEBUG_TIMER_START(debugTimerGuiMain);
  guiMain(evt);
  DEBUG_TIMER_STOP(debugTimerGuiMain);
#endif

#if defined(PCBTARANIS)
  if (mainRequestFlags & (1 << REQUEST_SCREENSHOT)) {
    writeScreenshot();
    mainRequestFlags &= ~(1 << REQUEST_SCREENSHOT);
  }
#endif

#if defined(PCBX9E) && !defined(SIMU)
  toplcdRefreshStart();
  setTopFirstTimer(getValue(MIXSRC_FIRST_TIMER+g_model.toplcdTimer));
  setTopSecondTimer(g_eeGeneral.globalTimer + sessionTimer);
  setTopRssi(TELEMETRY_RSSI());
  setTopBatteryValue(g_vbat100mV);
  setTopBatteryState(GET_TXBATT_BARS(), IS_TXBATT_WARNING());
  toplcdRefreshEnd();
#endif

#if defined(BLUETOOTH) && !defined(SIMU)
  bluetoothWakeup();
#endif

#if defined(INTERNAL_GPS)
  gpsWakeup();
#endif
}

