/*
 *  @ 2023 Travis Farmer
 *  © 2023 Neil McKechnie
 *  © 2022-2023 Paul M. Antoine
 *  © 2021 Mike S
 *  © 2021, 2023 Harald Barth
 *  © 2021 Fred Decker
 *  © 2021 Chris Harlow
 *  © 2021 David Cutting
 *  All rights reserved.
 *  
 *  This file is part of Asbelos DCC API
 *
 *  This is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  It is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with CommandStation.  If not, see <https://www.gnu.org/licenses/>.
 */

// ATTENTION: this file only compiles on a STM32 based boards
// Please refer to DCCTimer.h for general comments about how this class works
// This is to avoid repetition and duplication.
#if defined(ARDUINO_GIGA)

#include "DCCTimer.h"
#include "DIAG.h"
#include "Portenta_H7_TimerInterrupt.h"
///////////////////////////////////////////////////////////////////////////////////////////////
// Experimental code for High Accuracy (HA) DCC Signal mode
// Warning - use of TIM2 and TIM3 can affect the use of analogWrite() function on certain pins,
// which is used by the DC motor types.
///////////////////////////////////////////////////////////////////////////////////////////////

INTERRUPT_CALLBACK interruptHandler=0;

//HardwareTimer*  timer = NULL;
//HardwareTimer*  timerAux = NULL;
HardwareTimer timer(TIM2);
HardwareTimer timerAux(TIM3);
static bool tim2ModeHA = false;
static bool tim3ModeHA = false;

void DCCTimer_Handler() __attribute__((interrupt));

void DCCTimer_Handler() {
  interruptHandler();
}

void DCCTimer::begin(INTERRUPT_CALLBACK callback) {
  interruptHandler=callback;
    noInterrupts();
    
 // adc_set_sample_rate(ADC_SAMPLETIME_480CYCLES);
    timer.pause();
    timerAux.pause();
    timer.setPrescaleFactor(1);
    timer.setOverflow(DCC_SIGNAL_TIME, MICROSEC_FORMAT);
    timer.attachInterrupt(DCCTimer_Handler);
    timer.refresh();
    timerAux.setPrescaleFactor(1);
    timerAux.setOverflow(DCC_SIGNAL_TIME, MICROSEC_FORMAT);
    timerAux.refresh();

    timer.resume();
    timerAux.resume();

    interrupts();
}

bool DCCTimer::isPWMPin(byte pin) {
    switch (pin) {
     case 12:
       return true;
     case 13:
       return true;
     default:
       return false;
    }
}

void DCCTimer::setPWM(byte pin, bool high) {
    switch (pin) {
     case 9:
       if (!tim3ModeHA) {
         timerAux.setMode(1, TIMER_OUTPUT_COMPARE_INACTIVE, 9);
         tim3ModeHA = true;
       }
       if (high) 
         TIM17->CCMR1 = (TIM17->CCMR1 & ~TIM_CCMR1_OC1M_Msk) | TIM_CCMR1_OC1M_0;
       else
         TIM17->CCMR1 = (TIM17->CCMR1 & ~TIM_CCMR1_OC1M_Msk) | TIM_CCMR1_OC1M_1;
       break;
     case 8:
       if (!tim2ModeHA) {
         timer.setMode(1, TIMER_OUTPUT_COMPARE_INACTIVE, 8);
         tim2ModeHA = true;
       }
       if (high) 
         TIM16->CCMR1 = (TIM16->CCMR1 & ~TIM_CCMR1_OC1M_Msk) | TIM_CCMR1_OC1M_0;
       else
         TIM16->CCMR1 = (TIM16->CCMR1 & ~TIM_CCMR1_OC1M_Msk) | TIM_CCMR1_OC1M_1;
       break;
   }
 }

void DCCTimer::clearPWM() {
  timer.setMode(1, TIMER_OUTPUT_COMPARE_INACTIVE, NC);
  tim2ModeHA = false;
  timerAux.setMode(1, TIMER_OUTPUT_COMPARE_INACTIVE, NC);  
  tim3ModeHA = false;
}

void   DCCTimer::getSimulatedMacAddress(byte mac[6]) {
  volatile uint32_t *serno1 = (volatile uint32_t *)0x1FFF7A10;
  volatile uint32_t *serno2 = (volatile uint32_t *)0x1FFF7A14;
  // volatile uint32_t *serno3 = (volatile uint32_t *)0x1FFF7A18;

  volatile uint32_t m1 = *serno1;
  volatile uint32_t m2 = *serno2;
  mac[0] = m1 >> 8;
  mac[1] = m1 >> 0;
  mac[2] = m2 >> 24;
  mac[3] = m2 >> 16;
  mac[4] = m2 >> 8;
  mac[5] = m2 >> 0;
}
volatile int DCCTimer::minimum_free_memory=__INT_MAX__;

// Return low memory value... 
int DCCTimer::getMinimumFreeMemory() {
  noInterrupts(); // Disable interrupts to get volatile value 
  int retval = freeMemory();
  interrupts();
  return retval;
}
extern "C" char* sbrk(int incr);

int DCCTimer::freeMemory() {
  
  char top;
  unsigned int tmp = (unsigned int)(&top - reinterpret_cast<char*>(sbrk(0)));
  return (int)(tmp / 1000);
}

void DCCTimer::reset() {
  //Watchdog &watchdog = Watchdog::get_instance();
  //Watchdog::stop();
  //Watchdog::start(500);
  
  //while(true) {};
}

int16_t ADCee::ADCmax()
{
    return 1023;
}

int ADCee::init(uint8_t pin) {
  return analogRead(pin);
}

/*
 * Read function ADCee::read(pin) to get value instead of analogRead(pin)
 */
int ADCee::read(uint8_t pin, bool fromISR) {
  int current;
  if (!fromISR) noInterrupts();
  current = analogRead(pin);
  if (!fromISR) interrupts();
  return current;
}

/*
 * Scan function that is called from interrupt
 */
#pragma GCC push_options
#pragma GCC optimize ("-O3")
void ADCee::scan() {
}
#pragma GCC pop_options

void ADCee::begin() {
  noInterrupts();
  interrupts();
}
#endif
