/**************************************************************************
 * Name:    Timothy Lamb                                                  *
 * Email:   trash80@gmail.com                                             *
 ***************************************************************************/
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

unsigned long lastClockTickTime = 0;
bool disableStartMessages = false;
const unsigned long clockTimeout = 200; 

bool waitingForValue = false;
bool skipNextClock = false;
byte pendingNoteIndex = 0;
unsigned long pendingNoteTime = 0;
const unsigned long midiValueTimeout = 5;

byte tickCount = 0;

void modeLSDJMidioutSetup()
{
  digitalWrite(pinStatusLed,LOW);
  pinMode(pinGBClock,OUTPUT);
  digitalWrite(pinGBClock,HIGH);

  countGbClockTicks=0;
  lastMidiData[0] = -1;
  lastMidiData[1] = -1;
  midiValueMode = false;
  blinkMaxCount=60;
  modeLSDJMidiout();
}

void modeLSDJMidiout()
{
  while(1) {
    // we are stopping our secondary sequencers only when there are no clock ticks for a certain period
    if (disableStartMessages && (millis() - lastClockTickTime > clockTimeout)) {
      MIDI_sendRealTime(midi::Stop);
      stopAllNotes(); // need to rework; notes are stuck when played without clock(!!!!)
      disableStartMessages = false;
      tickCount = 0;
    }

    if(getIncommingSlaveByte()) {
      if (incomingMidiByte >= 0xF8) {
        continue;
      }

      if (waitingForValue && incomingMidiByte < 0x70) {
        waitingForValue = false;
        midioutDoAction(pendingNoteIndex, incomingMidiByte);
        continue;
      }

      if(incomingMidiByte > 0x6f && incomingMidiByte <= 0x7F) {
        switch(incomingMidiByte) {
          case 0xB3: case 0x7C:
            break;
          case 0x78: case 0x79: case 0x7A: case 0x7F:
            break;
          case 0x7E: // stop
            break;
          case 0x7D:
          //  We are starting our secondary sequencers only on clock ticks now
          //  MIDI_sendRealTime(midi::Start);
            break;
          default:
            pendingNoteIndex = incomingMidiByte - 0x70;
            pendingNoteTime = millis();
            waitingForValue = true;
            break;
        }
      } else {
        waitingForValue = false;
      }
    } else {
      setMode();
    }

    if (waitingForValue && (millis() - pendingNoteTime > midiValueTimeout)) {
      waitingForValue = false;
    }
  }
}

void midioutDoAction(byte m, byte v)
{
  if(m < 4) {
    if(v) {
      checkStopNote(m);
      playNote(m,v);
      if (m == 3) { 
        // perform correction based on the noise channel notes only
        performTicksPhaseCorrection();
      }
    } else if (midiOutLastNote[m]>=0) {
      stopNote(m);
    }
  } else if (m < 8) {
    //
  } else if(m < 0x0C) {
    if (skipNextClock == false) {
    sendClock();
    } else {
      skipNextClock = false;
    }
  }
}

void performTicksPhaseCorrection() {
      int mod = tickCount % 6;
      switch(mod) {
        case 0:
          tickCount = 0;
        case 1: 
        case 2:  
        case 3: 
          break;
        case 4: 
        case 5:
          sendClock();
      }
}

void sendClock() {
  if (!disableStartMessages) {
    MIDI_sendRealTime(midi::Start);
  }
    MIDI_sendRealTime(midi::Clock);
    tickCount++;
    disableStartMessages = true;
    lastClockTickTime = millis();
}

void checkStopNote(byte m)
{
  if((midioutNoteTimer[m]+midioutNoteTimerThreshold) < millis()) {
    stopNote(m);
  }
}

void stopNote(byte m)
{
  for(int x=0;x<midioutNoteHoldCounter[m];x++) {
    MIDI_sendNoteOff(midioutNoteHold[m][x], 0, memory[MEM_MIDIOUT_NOTE_CH+m]+1);
  }
  midiOutLastNote[m] = -1;
  midioutNoteHoldCounter[m] = 0;
}

void playNote(byte m, byte n)
{
  MIDI_sendNoteOn(n, 127, memory[MEM_MIDIOUT_NOTE_CH+m]+1);

  midioutNoteHold[m][midioutNoteHoldCounter[m]] =n;
  midioutNoteHoldCounter[m]++;
  midioutNoteTimer[m] = millis();
  midiOutLastNote[m] =n;
}

// void playCC(byte m, byte n)
// {
//   byte v = n;

//   if(memory[MEM_MIDIOUT_CC_MODE+m]) {
//     if(memory[MEM_MIDIOUT_CC_SCALING+m]) {
//       v = (v & 0x0F)*8;
//       //if(v) v --;
//     }
//     n=(m*7)+((n>>4) & 0x07);
//     MIDI_sendControlChange((memory[MEM_MIDIOUT_CC_NUMBERS+n]), v, memory[MEM_MIDIOUT_NOTE_CH+m]+1);
//   } else {
//     if(memory[MEM_MIDIOUT_CC_SCALING+m]) {
//       float s;
//       s = n;
//       v = ((s / 0x6f) * 0x7f);
//     }
//     n=(m*7);
    
//     MIDI_sendControlChange((memory[MEM_MIDIOUT_CC_NUMBERS+n]), v, memory[MEM_MIDIOUT_NOTE_CH+m]+1);
//   }
// }

// void playPC(byte m, byte n)
// {
//   MIDI_sendProgramChange(n, memory[MEM_MIDIOUT_NOTE_CH+m]+1);
// }

void stopAllNotes()
{
  for(int m=0;m<4;m++) {
    if(midiOutLastNote[m]>=0) {
      stopNote(m);
    }
    MIDI_sendControlChange(123, 127, memory[MEM_MIDIOUT_NOTE_CH+m]+1);
  }
}

boolean getIncommingSlaveByte()
{
  delayMicroseconds(midioutBitDelay);
  GB_SET(0,0,0);
  delayMicroseconds(midioutBitDelay);
  GB_SET(1,0,0);
  delayMicroseconds(2);
  if(digitalRead(pinGBSerialIn)) {
    incomingMidiByte = 0;
    for(countClockPause=0;countClockPause!=7;countClockPause++) {
      GB_SET(0,0,0);
      delayMicroseconds(2);
      GB_SET(1,0,0);
      incomingMidiByte = (incomingMidiByte << 1) + digitalRead(pinGBSerialIn);
    }
    return true;
  }
  return false;
}
