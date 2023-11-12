/*
 *  © 2021 Neil McKechnie
 *  © 2020-2021 Harald Barth
 *  © 2020-2021 Chris Harlow
 *  All rights reserved.
 *  
 *  This file is part of CommandStation-EX
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
/**********************************************************************

DCC++ BASE STATION supports Sensor inputs that can be connected to any Arduino Pin
not in use by this program.  Sensors can be of any type (infrared, magentic, mechanical...).
The only requirement is that when "activated" the Sensor must force the specified Arduino
Pin LOW (i.e. to ground), and when not activated, this Pin should remain HIGH (e.g. 5V),
or be allowed to float HIGH if use of the Arduino Pin's internal pull-up resistor is specified.

To ensure proper voltage levels, some part of the Sensor circuitry
MUST be tied back to the same ground as used by the Arduino.

The Sensor code below utilises "de-bounce" logic to remove spikes generated by
mechanical switches and transistors.  This avoids the need to create smoothing circuitry
for each sensor.  You may need to change the parameters through trial and error for your specific sensors.

To have this sketch monitor one or more Arduino pins for sensor triggers, first define/edit/delete
sensor definitions using the following variation of the "S" command:

  <S ID PIN PULLUP>:           creates a new sensor ID, with specified PIN and PULLUP
                               if sensor ID already exists, it is updated with specificed PIN and PULLUP
                               returns: <O> if successful and <X> if unsuccessful (e.g. out of memory)

  <S ID>:                      deletes definition of sensor ID
                               returns: <O> if successful and <X> if unsuccessful (e.g. ID does not exist)

  <S>:                         lists all defined sensors
                               returns: <Q ID PIN PULLUP> for each defined sensor or <X> if no sensors defined

where

  ID: the numeric ID (0-32767) of the sensor
  PIN: the arduino pin number the sensor is connected to
  PULLUP: 1=use internal pull-up resistor for PIN, 0=don't use internal pull-up resistor for PIN

Once all sensors have been properly defined, use the <E> command to store their definitions to EEPROM.
If you later make edits/additions/deletions to the sensor definitions, you must invoke the <E> command if you want those
new definitions updated in the EEPROM.  You can also clear everything stored in the EEPROM by invoking the <e> command.

All sensors defined as per above are repeatedly and sequentially checked within the main loop of this sketch.
If a Sensor Pin is found to have transitioned from one state to another, one of the following serial messages are generated:

  <Q ID>     - for transition of Sensor ID from HIGH state to LOW state (i.e. the sensor is triggered)
  <q ID>     - for transition of Sensor ID from LOW state to HIGH state (i.e. the sensor is no longer triggered)

Depending on whether the physical sensor is acting as an "event-trigger" or a "detection-sensor," you may
decide to ignore the <q ID> return and only react to <Q ID> triggers.

**********************************************************************/

#include "StringFormatter.h"
#include "CommandDistributor.h"
#include "Sensors.h"
#ifndef DISABLE_EEPROM
#include "EEStore.h"
#endif
#include "IODevice.h"


///////////////////////////////////////////////////////////////////////////////
// checks a number of defined sensors per entry and prints _changed_ sensor state
// to stream unless stream is NULL in which case only internal
// state is updated. Then advances to next sensor which will
// be checked at next invocation.  Each cycle of reading all sensors will
// be initiated no more frequently than the time set by 'cycleInterval' microseconds.
//
// The list of sensors is divided such that the first part of the list
// contains sensors that support change notification via callback, and the second
// part of the list contains sensors that require cyclic polling.  The start of the
// second part of the list is determined from by the 'firstPollSensor' pointer.
///////////////////////////////////////////////////////////////////////////////

void Sensor::checkAll(){
  uint16_t sensorCount = 0;

#ifdef USE_NOTIFY
  // Register the event handler ONCE!
  if (!inputChangeCallbackRegistered)
    IONotifyCallback::add(inputChangeCallback);
  inputChangeCallbackRegistered = true;
#endif

  if (firstSensor == NULL) return;  // No sensors to be scanned
  if (readingSensor == NULL) { 
    // Not currently scanning sensor list
    unsigned long thisTime = micros();
    if (thisTime - lastReadCycle >= cycleInterval) {
      // Required time elapsed since last read cycle started,
      // so initiate new scan through the sensor list
      readingSensor = firstSensor;
      lastReadCycle = thisTime;
    }
  }

  // Loop until either end of list is encountered or we pause for some reason
  bool pause = false;
  while (readingSensor != NULL && !pause) {

    // Where the sensor is attached to a pin, read pin status.  For sources such as LCN,
    // which don't have an input pin to read, the LCN class calls setState() to update inputState when
    // a message is received.  The IODevice::read() call returns 1 for active pins (0v) and 0 for inactive (5v).
    // Also, on HAL drivers that support change notifications, the driver calls the notification callback
    // routine when an input signal change is detected, and this updates the inputState directly,
    // so these inputs don't need to be polled here.
    VPIN pin = readingSensor->data.pin;
    if (readingSensor->pollingRequired && pin != VPIN_NONE)
      readingSensor->inputState = IODevice::read(pin);

    // Check if changed since last time, and process changes.
    if (readingSensor->inputState == readingSensor->active) {
      // no change
      readingSensor->latchDelay = minReadCount; // Reset counter
    } else if (readingSensor->latchDelay > 0) {
      // change detected, but first decrement delay
      readingSensor->latchDelay--;
    } else { 
      // change validated, act on it.
      readingSensor->active = readingSensor->inputState;
      readingSensor->latchDelay = minReadCount;  // Reset counter
      
      CommandDistributor::broadcastSensor(readingSensor->data.snum,readingSensor->active);
      pause = true;  // Don't check any more sensors on this entry
    }

    // Move to next sensor in list.
    readingSensor = readingSensor->nextSensor;

    // Currently process max of 16 sensors per entry.
    // Performance measurements taken during development indicate that, with 128 sensors configured
    // on 8x 16-pin MCP23017 GPIO expanders with polling (no change notification), all inputs can be read from the devices
    // within 1.4ms (400Mhz I2C bus speed), and a full cycle of checking 128 sensors for changes takes under a millisecond.
    sensorCount++;
    if (sensorCount >= 16) pause = true;
  }

} // Sensor::checkAll


#ifdef USE_NOTIFY
// Callback from HAL (IODevice class) when a digital input change is recognised.
// Updates the inputState field, which is subsequently scanned for changes in the checkAll 
// method.  Ideally the <Q>/<q> message should be sent from here, instead of waiting for
// the checkAll method, but the output stream is not available at this point.
void Sensor::inputChangeCallback(VPIN vpin, int state) {
  Sensor *tt;
  // This bit is not ideal since it has, potentially, to look through the entire list of
  // sensors to find the one that has changed.  Ideally this should be improved somehow.
  for (tt=firstSensor; tt!=NULL ; tt=tt->nextSensor) {
    if (tt->data.pin == vpin) break;
  }
  if (tt != NULL) { // Sensor found
    tt->inputState = (state != 0); 
  }
}
#endif

///////////////////////////////////////////////////////////////////////////////
//
// prints all sensor states to stream
//
///////////////////////////////////////////////////////////////////////////////

void Sensor::printAll(Print *stream){

  if (stream != NULL) {
    for(Sensor * tt=firstSensor;tt!=NULL;tt=tt->nextSensor){
      StringFormatter::send(stream, F("<%c %d>\n"), tt->active ? 'Q' : 'q', tt->data.snum);
    }
  } // loop over all sensors
} // Sensor::printAll

///////////////////////////////////////////////////////////////////////////////
// Static Function to create/find Sensor object.

Sensor *Sensor::create(int snum, VPIN pin, int pullUp){
  Sensor *tt;

  if (pin > VPIN_MAX && pin != VPIN_NONE) return NULL;

  remove(snum);  // Unlink and free any existing sensor with the same id, before creating the new one.

  tt = (Sensor *)calloc(1,sizeof(Sensor));
  if (!tt) return tt;     // memory allocation failure

  if (pin == VPIN_NONE) 
    tt->pollingRequired = false;
  #ifdef USE_NOTIFY
  else if (IODevice::hasCallback(pin)) 
    tt->pollingRequired = false;
  #endif
  else 
    tt->pollingRequired = true;

  // Add to the start of the list
  tt->nextSensor = firstSensor;
  firstSensor = tt;

  tt->data.snum = snum;
  tt->data.pin = pin;
  tt->data.pullUp = pullUp;
  tt->active = 0;
  tt->inputState = 0;
  tt->latchDelay = minReadCount;

  if (pin != VPIN_NONE) 
    IODevice::configureInput(pin, pullUp);   
    // Generally, internal pull-up resistors are not, on their own, sufficient 
    // for external infrared sensors --- each sensor must have its own 1K external pull-up resistor

  return tt;
}

///////////////////////////////////////////////////////////////////////////////
// Object method to directly change the input state, for sensors such as LCN which are updated
//  by means other than by polling an input.

void Sensor::setState(int value) {
  // Trigger sensor change to be reported on next checkAll loop.
  inputState = (value != 0);
  latchDelay = 0; // Don't wait for anti-jitter logic
}

///////////////////////////////////////////////////////////////////////////////

Sensor* Sensor::get(int n){
  Sensor *tt;
  for(tt=firstSensor;tt!=NULL && tt->data.snum!=n;tt=tt->nextSensor);
  return tt ;
}
///////////////////////////////////////////////////////////////////////////////

bool Sensor::remove(int n){
  Sensor *tt,*pp=NULL;

  for(tt=firstSensor;tt!=NULL && tt->data.snum!=n;pp=tt,tt=tt->nextSensor);

  if (tt==NULL)  return false;

  // Unlink the sensor from the list
  if(tt==firstSensor) 
    firstSensor=tt->nextSensor;
  else 
    pp->nextSensor=tt->nextSensor;
#ifdef USE_NOTIFY
  if (tt==lastSensor)
    lastSensor = pp;
  if (tt==firstPollSensor)
    firstPollSensor = tt->nextSensor;
#endif

  // Check if the sensor being deleted is the next one to be read.  If so, 
  // make the following one the next one to be read.
  if (readingSensor==tt) readingSensor=tt->nextSensor;

  free(tt);

  return true;
}

///////////////////////////////////////////////////////////////////////////////
#ifndef DISABLE_EEPROM
void Sensor::load(){
  struct SensorData data;
  Sensor *tt;

  uint16_t i=EEStore::eeStore->data.nSensors;
  while(i--){
    EEPROM.get(EEStore::pointer(),data);
    tt=create(data.snum, data.pin, data.pullUp);
    EEStore::advance(sizeof(tt->data));
  }
}

///////////////////////////////////////////////////////////////////////////////

void Sensor::store(){
  Sensor *tt;

  tt=firstSensor;
  EEStore::eeStore->data.nSensors=0;

  while(tt!=NULL){
    EEPROM.put(EEStore::pointer(),tt->data);
#ifdef ARDUINO_ARCH_ESP32
    EEPROM.commit();
#endif
    EEStore::advance(sizeof(tt->data));
    tt=tt->nextSensor;
    EEStore::eeStore->data.nSensors++;
  }
}
#endif
///////////////////////////////////////////////////////////////////////////////

Sensor *Sensor::firstSensor=NULL;
Sensor *Sensor::readingSensor=NULL;
unsigned long Sensor::lastReadCycle=0;

#ifdef USE_NOTIFY
Sensor *Sensor::firstPollSensor = NULL;
Sensor *Sensor::lastSensor = NULL;
bool Sensor::inputChangeCallbackRegistered = false;
#endif
