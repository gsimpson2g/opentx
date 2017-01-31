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
#include <errno.h>
#include <stdarg.h>
#include <string>

#if !defined _MSC_VER || defined __GNUC__
  #include <sys/time.h>
#endif

#if defined(SIMU_DISKIO)
  FILE * diskImage = 0;
#endif

#if defined(SIMU_AUDIO) && defined(CPUARM)
  #include <SDL.h>
#endif

uint8_t MCUCSR, MCUSR, MCUCR;
volatile uint8_t pina=0xff, pinb=0xff, pinc=0xff, pind, pine=0xff, pinf=0xff, ping=0xff, pinh=0xff, pinj=0, pinl=0;
uint8_t portb, portc, porth=0, dummyport;
uint16_t dummyport16;
int g_snapshot_idx = 0;

pthread_t main_thread_pid;
uint8_t main_thread_running = 0;
char * main_thread_error = NULL;

#if defined(STM32)
uint32_t Peri1_frequency, Peri2_frequency;
GPIO_TypeDef gpioa, gpiob, gpioc, gpiod, gpioe, gpiof, gpiog, gpioh, gpioi, gpioj;
TIM_TypeDef tim1, tim2, tim3, tim4, tim5, tim6, tim7, tim8, tim9, tim10;
RCC_TypeDef rcc;
DMA_Stream_TypeDef dma1_stream2, dma1_stream5, dma1_stream7, dma2_stream1, dma2_stream2, dma2_stream5, dma2_stream6;
DMA_TypeDef dma2;
USART_TypeDef Usart0, Usart1, Usart2, Usart3, Usart4;
SysTick_Type systick;
#elif defined(CPUARM)
Pio Pioa, Piob, Pioc;
Pmc pmc;
Ssc ssc;
Pwm pwm;
Twi Twio;
Usart Usart0;
Dacc dacc;
Adc Adc0;
#endif

void lcdInit()
{
}

void toplcdOff()
{
}

uint16_t getTmr16KHz()
{
#if defined(_MSC_VER)
  return get_tmr10ms() * 16;
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_usec * 2 / 125;
#endif
}

uint16_t getTmr2MHz()
{
#if defined(_MSC_VER)
  return get_tmr10ms() * 125;
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_usec * 2;
#endif
}

U64 CoGetOSTime(void)
{
  // one tick is 2ms
#if defined(_MSC_VER)
  return GetTickCount()/2;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((U64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000) /2;
#endif
}

void simuInit()
{
#if defined(STM32)
  RCC->CSR = 0;
#endif

  for (int i = 0; i <= 17; i++) {
    simuSetSwitch(i, 0);
    simuSetKey(i, false);  // a little dirty, but setting keys that don't exist is perfectly OK here
  }
}

#define NEG_CASE(sw_or_key, pin, mask) \
    case sw_or_key: \
      if ((int)state > 0) pin &= ~(mask); else pin |= (mask); \
      break;
#define POS_CASE(sw_or_key, pin, mask) \
    case sw_or_key: \
      if ((int)state > 0) pin |= (mask); else pin &= ~(mask); \
      break;

#if defined(CPUARM)
  #if defined(PCBHORUS)
    #define SWITCH_CASE NEG_CASE
    #define SWITCH_CASE_NEGATIVE POS_CASE
  #elif defined(PCBTARANIS) && !defined(PCBX9E)
    #define SWITCH_CASE NEG_CASE
  #else
    #define SWITCH_CASE POS_CASE
  #endif
  #define SWITCH_3_CASE(swtch, pin1, pin2, mask1, mask2) \
    case swtch: \
      if ((int)state < 0) pin1 &= ~(mask1); else pin1 |= (mask1); \
      if ((int)state > 0) pin2 &= ~(mask2); else pin2 |= (mask2); \
      break;
  #define KEY_CASE NEG_CASE
  #define TRIM_CASE NEG_CASE
#else
  #if defined(PCBMEGA2560)
    #define SWITCH_CASE POS_CASE
  #else
    #define SWITCH_CASE NEG_CASE
  #endif
  #define SWITCH_3_CASE(swtch, pin1, pin2, mask1, mask2) \
    case swtch: \
      if ((int)state >= 0) pin1 &= ~(mask1); else pin1 |= (mask1); \
      if ((int)state <= 0) pin2 &= ~(mask2); else pin2 |= (mask2); \
      break;
  #define KEY_CASE POS_CASE
  #define TRIM_CASE KEY_CASE
#endif

void simuSetKey(uint8_t key, bool state)
{
  // if (state) TRACE_SIMPGMSPACE("simuSetKey(%d, %d)", key, state);

  switch (key) {
#if !defined(PCBHORUS)
    KEY_CASE(KEY_MENU, KEYS_GPIO_REG_MENU, KEYS_GPIO_PIN_MENU)
    KEY_CASE(KEY_EXIT, KEYS_GPIO_REG_EXIT, KEYS_GPIO_PIN_EXIT)
#endif
#if defined(PCBX12S)
    KEY_CASE(KEY_PGUP, KEYS_GPIO_REG_PGUP, KEYS_GPIO_PIN_PGUP)
    KEY_CASE(KEY_PGDN, KEYS_GPIO_REG_PGDN, KEYS_GPIO_PIN_PGDN)
    KEY_CASE(KEY_ENTER, KEYS_GPIO_REG_ENTER, KEYS_GPIO_PIN_ENTER)
    KEY_CASE(KEY_TELEM, KEYS_GPIO_REG_RIGHT, KEYS_GPIO_PIN_RIGHT)
    KEY_CASE(KEY_RADIO, KEYS_GPIO_REG_LEFT, KEYS_GPIO_PIN_LEFT)
    KEY_CASE(KEY_MODEL, KEYS_GPIO_REG_UP, KEYS_GPIO_PIN_UP)
    KEY_CASE(KEY_EXIT, KEYS_GPIO_REG_DOWN, KEYS_GPIO_PIN_DOWN)
#elif defined(PCBX10)
    // KEY_CASE(KEY_PGUP, KEYS_GPIO_REG_MENU, KEYS_GPIO_PIN_MENU)
    // KEY_CASE(KEY_PGDN, KEYS_GPIO_REG_EXIT, KEYS_GPIO_PIN_EXIT)
    KEY_CASE(KEY_ENTER, KEYS_GPIO_REG_ENTER, KEYS_GPIO_PIN_ENTER)
    KEY_CASE(KEY_TELEM, KEYS_GPIO_REG_RIGHT, KEYS_GPIO_PIN_RIGHT)
    KEY_CASE(KEY_RADIO, KEYS_GPIO_REG_LEFT, KEYS_GPIO_PIN_LEFT)
    KEY_CASE(KEY_MODEL, KEYS_GPIO_REG_UP, KEYS_GPIO_PIN_UP)
    KEY_CASE(KEY_EXIT, KEYS_GPIO_REG_DOWN, KEYS_GPIO_PIN_DOWN)
#elif defined(PCBFLAMENCO)
    KEY_CASE(KEY_ENTER, KEYS_GPIO_REG_ENTER, KEYS_GPIO_PIN_ENTER)
    KEY_CASE(KEY_PAGE, KEYS_GPIO_REG_PAGE, KEYS_GPIO_PIN_PAGE)
#elif defined(PCBTARANIS)
    KEY_CASE(KEY_ENTER, KEYS_GPIO_REG_ENTER, KEYS_GPIO_PIN_ENTER)
    KEY_CASE(KEY_PAGE, KEYS_GPIO_REG_PAGE, KEYS_GPIO_PIN_PAGE)
    KEY_CASE(KEY_MINUS, KEYS_GPIO_REG_MINUS, KEYS_GPIO_PIN_MINUS)
    KEY_CASE(KEY_PLUS, KEYS_GPIO_REG_PLUS, KEYS_GPIO_PIN_PLUS)
#else
    KEY_CASE(KEY_RIGHT, KEYS_GPIO_REG_RIGHT, KEYS_GPIO_PIN_RIGHT)
    KEY_CASE(KEY_LEFT, KEYS_GPIO_REG_LEFT, KEYS_GPIO_PIN_LEFT)
    KEY_CASE(KEY_UP, KEYS_GPIO_REG_UP, KEYS_GPIO_PIN_UP)
    KEY_CASE(KEY_DOWN, KEYS_GPIO_REG_DOWN, KEYS_GPIO_PIN_DOWN)
#endif
#if defined(PCBSKY9X) && !defined(REVX) && !defined(AR9X)
    KEY_CASE(BTN_REa, PIOB->PIO_PDSR, 0x40)
#elif defined(PCBGRUVIN9X) || defined(PCBMEGA2560)
    KEY_CASE(BTN_REa, pind, 0x20)
#elif defined(PCB9X) && defined(ROTARY_ENCODER_NAVIGATION)
    KEY_CASE(BTN_REa, RotEncoder, 0x20)
#endif
  }
}

void simuSetTrim(uint8_t trim, bool state)
{
  // TRACE_SIMPGMSPACE("trim=%d state=%d", trim, state);

  switch (trim) {
    TRIM_CASE(0, TRIMS_GPIO_REG_LHL, TRIMS_GPIO_PIN_LHL)
    TRIM_CASE(1, TRIMS_GPIO_REG_LHR, TRIMS_GPIO_PIN_LHR)
    TRIM_CASE(2, TRIMS_GPIO_REG_LVD, TRIMS_GPIO_PIN_LVD)
    TRIM_CASE(3, TRIMS_GPIO_REG_LVU, TRIMS_GPIO_PIN_LVU)
    TRIM_CASE(4, TRIMS_GPIO_REG_RVD, TRIMS_GPIO_PIN_RVD)
    TRIM_CASE(5, TRIMS_GPIO_REG_RVU, TRIMS_GPIO_PIN_RVU)
    TRIM_CASE(6, TRIMS_GPIO_REG_RHL, TRIMS_GPIO_PIN_RHL)
    TRIM_CASE(7, TRIMS_GPIO_REG_RHR, TRIMS_GPIO_PIN_RHR)
#if defined(PCBHORUS)
    TRIM_CASE(8, TRIMS_GPIO_REG_LSU, TRIMS_GPIO_PIN_LSU)
    TRIM_CASE(9, TRIMS_GPIO_REG_LSD, TRIMS_GPIO_PIN_RVU)
    TRIM_CASE(10, TRIMS_GPIO_REG_RSU, TRIMS_GPIO_PIN_RHL)
    TRIM_CASE(11, TRIMS_GPIO_REG_RSD, TRIMS_GPIO_PIN_RHR)
#endif
  }
}

// TODO use a better numbering to allow google tests to work on Taranis
void simuSetSwitch(uint8_t swtch, int8_t state)
{
  // TRACE_SIMPGMSPACE("simuSetSwitch(%d, %d)", swtch, state);

  switch (swtch) {
#if defined(PCBFLAMENCO)
    // SWITCH_3_CASE(0, SWITCHES_GPIO_REG_A_L, SWITCHES_GPIO_REG_A_H, SWITCHES_GPIO_PIN_A_L, SWITCHES_GPIO_PIN_A_H)
    // SWITCH_CASE(1, SWITCHES_GPIO_REG_B, SWITCHES_GPIO_PIN_B)
    // SWITCH_3_CASE(2, SWITCHES_GPIO_REG_C_L, SWITCHES_GPIO_REG_C_H, SWITCHES_GPIO_PIN_C_L, SWITCHES_GPIO_PIN_C_H)
    // SWITCH_3_CASE(3, SWITCHES_GPIO_REG_D_L, SWITCHES_GPIO_REG_D_H, SWITCHES_GPIO_PIN_D_L, SWITCHES_GPIO_PIN_D_H)
    // SWITCH_CASE(4, SWITCHES_GPIO_REG_E, SWITCHES_GPIO_PIN_E)
    // SWITCH_3_CASE(5, SWITCHES_GPIO_REG_F_H, SWITCHES_GPIO_REG_F_L, SWITCHES_GPIO_PIN_F_H, SWITCHES_GPIO_PIN_F_L)
#elif defined(PCBTARANIS) || defined(PCBHORUS)
    SWITCH_3_CASE(0,  SWITCHES_GPIO_REG_A_L, SWITCHES_GPIO_REG_A_H, SWITCHES_GPIO_PIN_A_L, SWITCHES_GPIO_PIN_A_H)
    SWITCH_3_CASE(1,  SWITCHES_GPIO_REG_B_L, SWITCHES_GPIO_REG_B_H, SWITCHES_GPIO_PIN_B_L, SWITCHES_GPIO_PIN_B_H)
    SWITCH_3_CASE(2,  SWITCHES_GPIO_REG_C_L, SWITCHES_GPIO_REG_C_H, SWITCHES_GPIO_PIN_C_L, SWITCHES_GPIO_PIN_C_H)
    SWITCH_3_CASE(3,  SWITCHES_GPIO_REG_D_L, SWITCHES_GPIO_REG_D_H, SWITCHES_GPIO_PIN_D_L, SWITCHES_GPIO_PIN_D_H)
#if defined(PCBX7)
    SWITCH_CASE(4, SWITCHES_GPIO_REG_F, SWITCHES_GPIO_PIN_F)
    SWITCH_CASE(5, SWITCHES_GPIO_REG_H, SWITCHES_GPIO_PIN_H)
#else
    SWITCH_3_CASE(4,  SWITCHES_GPIO_REG_E_L, SWITCHES_GPIO_REG_E_H, SWITCHES_GPIO_PIN_E_L, SWITCHES_GPIO_PIN_E_H)
#if defined(PCBHORUS)
    SWITCH_CASE_NEGATIVE(5, SWITCHES_GPIO_REG_F, SWITCHES_GPIO_PIN_F)
#else
    SWITCH_CASE(5, SWITCHES_GPIO_REG_F, SWITCHES_GPIO_PIN_F)
#endif
    SWITCH_3_CASE(6,  SWITCHES_GPIO_REG_G_L, SWITCHES_GPIO_REG_G_H, SWITCHES_GPIO_PIN_G_L, SWITCHES_GPIO_PIN_G_H)
    SWITCH_CASE(7, SWITCHES_GPIO_REG_H, SWITCHES_GPIO_PIN_H)
#endif
#if defined(PCBX9E)
    SWITCH_3_CASE(8,  SWITCHES_GPIO_REG_I_L, SWITCHES_GPIO_REG_I_H, SWITCHES_GPIO_PIN_I_L, SWITCHES_GPIO_PIN_I_H)
    SWITCH_3_CASE(9,  SWITCHES_GPIO_REG_J_L, SWITCHES_GPIO_REG_J_H, SWITCHES_GPIO_PIN_J_L, SWITCHES_GPIO_PIN_J_H)
    SWITCH_3_CASE(10, SWITCHES_GPIO_REG_K_L, SWITCHES_GPIO_REG_K_H, SWITCHES_GPIO_PIN_K_L, SWITCHES_GPIO_PIN_K_H)
    SWITCH_3_CASE(11, SWITCHES_GPIO_REG_L_L, SWITCHES_GPIO_REG_L_H, SWITCHES_GPIO_PIN_L_L, SWITCHES_GPIO_PIN_L_H)
    SWITCH_3_CASE(12, SWITCHES_GPIO_REG_M_L, SWITCHES_GPIO_REG_M_H, SWITCHES_GPIO_PIN_M_L, SWITCHES_GPIO_PIN_M_H)
    SWITCH_3_CASE(13, SWITCHES_GPIO_REG_N_L, SWITCHES_GPIO_REG_N_H, SWITCHES_GPIO_PIN_N_L, SWITCHES_GPIO_PIN_N_H)
    SWITCH_3_CASE(14, SWITCHES_GPIO_REG_O_L, SWITCHES_GPIO_REG_O_H, SWITCHES_GPIO_PIN_O_L, SWITCHES_GPIO_PIN_O_H)
    SWITCH_3_CASE(15, SWITCHES_GPIO_REG_P_L, SWITCHES_GPIO_REG_P_H, SWITCHES_GPIO_PIN_P_L, SWITCHES_GPIO_PIN_P_H)
    SWITCH_3_CASE(16, SWITCHES_GPIO_REG_Q_L, SWITCHES_GPIO_REG_Q_H, SWITCHES_GPIO_PIN_Q_L, SWITCHES_GPIO_PIN_Q_H)
    SWITCH_3_CASE(17, SWITCHES_GPIO_REG_R_L, SWITCHES_GPIO_REG_R_H, SWITCHES_GPIO_PIN_R_L, SWITCHES_GPIO_PIN_R_H)
#endif
#elif defined(PCBSKY9X)
    SWITCH_CASE(0, PIOC->PIO_PDSR, 1<<20)
    SWITCH_CASE(1, PIOA->PIO_PDSR, 1<<15)
    SWITCH_CASE(2, PIOC->PIO_PDSR, 1<<31)
    SWITCH_3_CASE(3, PIOC->PIO_PDSR, PIOC->PIO_PDSR, 0x00004000, 0x00000800)
    SWITCH_CASE(4, PIOA->PIO_PDSR, 1<<2)
    SWITCH_CASE(5, PIOC->PIO_PDSR, 1<<16)
    SWITCH_CASE(6, PIOC->PIO_PDSR, 1<<8)
#elif defined(PCBGRUVIN9X)
    SWITCH_CASE(0, ping, 1<<INP_G_ThrCt)
    SWITCH_CASE(1, ping, 1<<INP_G_RuddDR)
    SWITCH_CASE(2, pinc, 1<<INP_C_ElevDR)
    SWITCH_3_CASE(3, ping, pinb, (1<<INP_G_ID1), (1<<INP_B_ID2))
    SWITCH_CASE(4, pinc, 1<<INP_C_AileDR)
    SWITCH_CASE(5, ping, 1<<INP_G_Gear)
    SWITCH_CASE(6, pinb, 1<<INP_B_Trainer)
#elif defined(PCBMEGA2560)
    SWITCH_CASE(0, ping, 1<<INP_G_ThrCt)
    SWITCH_CASE(1, ping, 1<<INP_G_RuddDR)
    SWITCH_CASE(2, pinc, 1<<INP_L_ElevDR)
    SWITCH_3_CASE(3, pinc, pinc, (1<<INP_C_ID1), (1<<INP_C_ID2))
    SWITCH_CASE(4, pinc, 1<<INP_C_AileDR)
    SWITCH_CASE(5, ping, 1<<INP_G_Gear)
    SWITCH_CASE(6, pinb, 1<<INP_L_Trainer)
#else // PCB9X
#if defined(TELEMETRY_JETI) || defined(TELEMETRY_FRSKY) || defined(TELEMETRY_NMEA) || defined(TELEMETRY_ARDUPILOT) || defined(TELEMETRY_MAVLINK)
    SWITCH_CASE(0, pinc, 1<<INP_C_ThrCt)
    SWITCH_CASE(4, pinc, 1<<INP_C_AileDR)
#else
    SWITCH_CASE(0, pine, 1<<INP_E_ThrCt)
    SWITCH_CASE(4, pine, 1<<INP_E_AileDR)
#endif
    SWITCH_3_CASE(3, ping, pine, (1<<INP_G_ID1), (1<<INP_E_ID2))
    SWITCH_CASE(1, ping, 1<<INP_G_RuddDR)
    SWITCH_CASE(2, pine, 1<<INP_E_ElevDR)
    SWITCH_CASE(5, pine, 1<<INP_E_Gear)
    SWITCH_CASE(6, pine, 1<<INP_E_Trainer)
#endif

    default:
      break;
  }
}

void StartSimu(bool tests, const char * sdPath, const char * settingsPath)
{
  s_current_protocol[0] = 255;
  menuLevel = 0;

  main_thread_running = (tests ? 1 : 2); // TODO rename to simu_run_mode with #define

  simuFatfsSetPaths(sdPath, settingsPath);

  /*
    g_tmr10ms must be non-zero otherwise some SF functions (that use this timer as a marker when it was last executed)
    will be executed twice on startup. Normal radio does not see this issue because g_tmr10ms is already a big number
    before the first call to the Special Functions. Not so in the simulator.

    There is another issue, some other function static variables depend on this value. If simulator is started
    multiple times in one Companion session, they are set to their initial values only first time the simulator
    is started. Therefore g_tmr10ms must also be set to non-zero value only the first time, then it must be left
    alone to continue from the previous simulator session value. See the issue #2446

  */
  if (g_tmr10ms == 0) {
    g_tmr10ms = 1;
  }

#if defined(RTCLOCK)
  g_rtcTime = time(0);
#endif

#if defined(SIMU_EXCEPTIONS)
  signal(SIGFPE, sig);
  signal(SIGSEGV, sig);
  try {
#endif

  pthread_create(&main_thread_pid, NULL, &simuMain, NULL);

#if defined(SIMU_EXCEPTIONS)
  }
  catch (...) {
  }
#endif
}

void StopSimu()
{
  main_thread_running = 0;

#if defined(CPUARM)
  pthread_join(mixerTaskId, NULL);
  pthread_join(menusTaskId, NULL);
#endif
  pthread_join(main_thread_pid, NULL);
}

#if defined(CPUARM)
struct SimulatorAudio {
  int volumeGain;
  int currentVolume;
  uint16_t leftoverData[AUDIO_BUFFER_SIZE];
  int leftoverLen;
  bool threadRunning;
  pthread_t threadPid;
} simuAudio;
#endif

void audioConsumeCurrentBuffer()
{
}

#if defined(MASTER_VOLUME)
void setScaledVolume(uint8_t volume)
{
  simuAudio.currentVolume = 127 * volume * simuAudio.volumeGain / VOLUME_LEVEL_MAX / 10;
  // TRACE_SIMPGMSPACE("setVolume(): in: %u, out: %u", volume, simuAudio.currentVolume);
}

void setVolume(uint8_t volume)
{
}

int32_t getVolume()
{
  return 0;
}
#endif

#if defined(SIMU_AUDIO) && defined(CPUARM)
void copyBuffer(uint8_t * dest, const uint16_t * buff, unsigned int samples)
{
  for(unsigned int i=0; i<samples; i++) {
    int sample = ((int32_t)(uint32_t)(buff[i]) - 0x8000);  // conversion from uint16_t
    *((uint16_t*)dest) = (int16_t)((sample * simuAudio.currentVolume)/127);
    dest += 2;
  }
}

void fillAudioBuffer(void *udata, Uint8 *stream, int len)
{
  SDL_memset(stream, 0, len);

  if (simuAudio.leftoverLen) {
    int len1 = min(len/2, simuAudio.leftoverLen);
    copyBuffer(stream, simuAudio.leftoverData, len1);
    len -= len1*2;
    stream += len1*2;
    simuAudio.leftoverLen -= len1;
    // putchar('l');
    if (simuAudio.leftoverLen) return;		// buffer fully filled
  }

  if (audioQueue.buffersFifo.filledAtleast(len/(AUDIO_BUFFER_SIZE*2)+1) ) {
    while(true) {
      const AudioBuffer * nextBuffer = audioQueue.buffersFifo.getNextFilledBuffer();
      if (nextBuffer) {
        if (len >= nextBuffer->size*2) {
          copyBuffer(stream, nextBuffer->data, nextBuffer->size);
          stream += nextBuffer->size*2;
          len -= nextBuffer->size*2;
          // putchar('+');
          audioQueue.buffersFifo.freeNextFilledBuffer();
        }
        else {
          //partial
          copyBuffer(stream, nextBuffer->data, len/2);
          simuAudio.leftoverLen = (nextBuffer->size-len/2);
          memcpy(simuAudio.leftoverData, &nextBuffer->data[len/2], simuAudio.leftoverLen*2);
          len = 0;
          // putchar('p');
          audioQueue.buffersFifo.freeNextFilledBuffer();
          break;
        }
      }
      else {
        break;
      }
    }
  }

  //fill the rest of buffer with silence
  if (len > 0) {
    SDL_memset(stream, 0x8000, len);  // make sure this is silence.
    // putchar('.');
  }
}

void * audioThread(void *)
{
  /*
    Checking here if SDL audio was initialized is wrong, because
    the SDL_CloseAudio() de-initializes it.

    if ( !SDL_WasInit(SDL_INIT_AUDIO) ) {
      fprintf(stderr, "ERROR: couldn't initialize SDL audio support\n");
      return 0;
    }
  */

  SDL_AudioSpec wanted, have;

  /* Set the audio format */
  wanted.freq = AUDIO_SAMPLE_RATE;
  wanted.format = AUDIO_S16SYS;
  wanted.channels = 1;    /* 1 = mono, 2 = stereo */
  wanted.samples = AUDIO_BUFFER_SIZE*2;  /* Good low-latency value for callback */
  wanted.callback = fillAudioBuffer;
  wanted.userdata = NULL;

  /*
    SDL_OpenAudio() internally calls SDL_InitSubSystem(SDL_INIT_AUDIO),
    which initializes SDL Audio subsystem if necessary
  */
  if ( SDL_OpenAudio(&wanted, &have) < 0 ) {
    fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
    return 0;
  }
  SDL_PauseAudio(0);

  while (simuAudio.threadRunning) {
    audioQueue.wakeup();
    sleep(1);
  }
  SDL_CloseAudio();
  return 0;
}

void StartAudioThread(int volumeGain)
{
  simuAudio.leftoverLen = 0;
  simuAudio.threadRunning = true;
  simuAudio.volumeGain = volumeGain;
  setScaledVolume(VOLUME_LEVEL_DEF);

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  struct sched_param sp;
  sp.sched_priority = SCHED_RR;
  pthread_attr_setschedparam(&attr, &sp);
  pthread_create(&simuAudio.threadPid, &attr, &audioThread, NULL);
  return;
}

void StopAudioThread()
{
  simuAudio.threadRunning = false;
  pthread_join(simuAudio.threadPid, NULL);
}
#endif // #if defined(SIMU_AUDIO) && defined(CPUARM)

uint16_t stackAvailable()
{
  return 500;
}

bool simuLcdRefresh = true;
display_t simuLcdBuf[DISPLAY_BUFFER_SIZE];

#if !defined(PCBFLAMENCO) && !defined(PCBHORUS)
void lcdSetRefVolt(uint8_t val)
{
}
#endif

void adcPrepareBandgap()
{
}

#if defined(PCBTARANIS)
void lcdOff()
{
}
#endif

void lcdRefresh()
{
#if defined(PCBFLAMENCO)
  TW8823_SendScreen();
#endif

  memcpy(simuLcdBuf, displayBuf, sizeof(simuLcdBuf));
  simuLcdRefresh = true;
}

void telemetryPortInit()
{
}

#if !defined(PCBFLAMENCO)
display_t simuLcdBackupBuf[DISPLAY_BUFFER_SIZE];
void lcdStoreBackupBuffer()
{
  memcpy(simuLcdBackupBuf, displayBuf, sizeof(simuLcdBackupBuf));
}

int lcdRestoreBackupBuffer()
{
  memcpy(displayBuf, simuLcdBackupBuf, sizeof(displayBuf));
  return 1;
}
#endif

uint32_t pwroffPressed() { return false; }

#if defined(CPUARM) && !defined(PWR_BUTTON_DELAY)
uint32_t pwrCheck() { return true; }
#endif

#if defined(CPUARM)
void pwrOff() { }
#endif

#if defined(STM32)
void pwrInit() { }
int usbPlugged() { return false; }
void USART_DeInit(USART_TypeDef* ) { }
ErrorStatus RTC_SetTime(uint32_t RTC_Format, RTC_TimeTypeDef* RTC_TimeStruct) { return SUCCESS; }
ErrorStatus RTC_SetDate(uint32_t RTC_Format, RTC_DateTypeDef* RTC_DateStruct) { return SUCCESS; }
void RTC_GetTime(uint32_t RTC_Format, RTC_TimeTypeDef * RTC_TimeStruct)
{
  time_t tme;
  time(&tme);
  struct tm * timeinfo = localtime(&tme);
  RTC_TimeStruct->RTC_Hours = timeinfo->tm_hour;
  RTC_TimeStruct->RTC_Minutes = timeinfo->tm_min;
  RTC_TimeStruct->RTC_Seconds = timeinfo->tm_sec;
}

void RTC_GetDate(uint32_t RTC_Format, RTC_DateTypeDef * RTC_DateStruct)
{
  time_t tme;
  time(&tme);
  struct tm * timeinfo = localtime(&tme);
  RTC_DateStruct->RTC_Year = timeinfo->tm_year - TM_YEAR_BASE;
  RTC_DateStruct->RTC_Month = timeinfo->tm_mon + 1;
  RTC_DateStruct->RTC_Date = timeinfo->tm_mday;
}

void RTC_TimeStructInit(RTC_TimeTypeDef* RTC_TimeStruct) { }
void RTC_DateStructInit(RTC_DateTypeDef* RTC_DateStruct) { }
void PWR_BackupAccessCmd(FunctionalState NewState) { }
void PWR_BackupRegulatorCmd(FunctionalState NewState) { }
void RCC_RTCCLKConfig(uint32_t RCC_RTCCLKSource) { }
void RCC_APB1PeriphClockCmd(uint32_t RCC_APB1Periph, FunctionalState NewState) { }
void RCC_RTCCLKCmd(FunctionalState NewState) { }
ErrorStatus RTC_Init(RTC_InitTypeDef* RTC_InitStruct) { return SUCCESS; }
void USART_SendData(USART_TypeDef* USARTx, uint16_t Data) { }
FlagStatus USART_GetFlagStatus(USART_TypeDef* USARTx, uint16_t USART_FLAG) { return SET; }
void GPIO_PinAFConfig(GPIO_TypeDef* GPIOx, uint16_t GPIO_PinSource, uint8_t GPIO_AF) { }
void USART_Init(USART_TypeDef* USARTx, USART_InitTypeDef* USART_InitStruct) { }
void USART_Cmd(USART_TypeDef* USARTx, FunctionalState NewState) { }
void USART_DMACmd(USART_TypeDef* USARTx, uint16_t USART_DMAReq, FunctionalState NewState) { }
void USART_ITConfig(USART_TypeDef* USARTx, uint16_t USART_IT, FunctionalState NewState) { }
// void TIM_TimeBaseInit(TIM_TypeDef* TIMx, TIM_TimeBaseInitTypeDef* TIM_TimeBaseInitStruct) { }
// void TIM_OC1Init(TIM_TypeDef* TIMx, TIM_OCInitTypeDef* TIM_OCInitStruct) { }
void TIM_DMAConfig(TIM_TypeDef* TIMx, uint16_t TIM_DMABase, uint16_t TIM_DMABurstLength) { }
void TIM_DMACmd(TIM_TypeDef* TIMx, uint16_t TIM_DMASource, FunctionalState NewState) { }
void TIM_CtrlPWMOutputs(TIM_TypeDef* TIMx, FunctionalState NewState) { }
void RCC_PLLI2SConfig(uint32_t PLLI2SN, uint32_t PLLI2SR) { }
void RCC_PLLI2SCmd(FunctionalState NewState) { }
void RCC_I2SCLKConfig(uint32_t RCC_I2SCLKSource) { }
void SPI_I2S_DeInit(SPI_TypeDef* SPIx) { }
void I2S_Init(SPI_TypeDef* SPIx, I2S_InitTypeDef* I2S_InitStruct) { }
void I2S_Cmd(SPI_TypeDef* SPIx, FunctionalState NewState) { }
void SPI_I2S_ITConfig(SPI_TypeDef* SPIx, uint8_t SPI_I2S_IT, FunctionalState NewState) { }
void RCC_LSEConfig(uint8_t RCC_LSE) { }
void RCC_GetClocksFreq(RCC_ClocksTypeDef* RCC_Clocks) { };
FlagStatus RCC_GetFlagStatus(uint8_t RCC_FLAG) { return SET; }
ErrorStatus RTC_WaitForSynchro(void) { return SUCCESS; }
void unlockFlash() { }
void lockFlash() { }
void flashWrite(uint32_t *address, uint32_t *buffer) { SIMU_SLEEP(100); }
uint32_t isBootloaderStart(const uint8_t * block) { return 1; }
#endif // defined(PCBTARANIS)

#if defined(PCBFLAMENCO)
void i2cWriteTW8823(unsigned char, unsigned char) { }
uint8_t i2cReadBQ24195(uint8_t) { return 0; }
void i2cWriteBQ24195(uint8_t, uint8_t) { }
#endif

#if defined(PCBHORUS)
void LCD_ControlLight(uint16_t dutyCycle) { }
#endif

void serialPrintf(const char * format, ...) { }
void serialCrlf() { }
void serialPutc(char c) { }
uint16_t stackSize() { return 0; }

void * start_routine(void * attr)
{
  FUNCPtr task = (FUNCPtr)attr;
  task(NULL);
  return NULL;
}

OS_TID CoCreateTask(FUNCPtr task, void *argv, uint32_t parameter, void * stk, uint32_t stksize)
{
  pthread_t tid;
  pthread_create(&tid, NULL, start_routine, (void *)task);
  return tid;
}
