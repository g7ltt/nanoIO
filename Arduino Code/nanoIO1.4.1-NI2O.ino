/***********************************************************************

  nanoIO

  CW / FSK interface sketch for use either stand alone or with fldigi.

  It is designed to fit within the RAM and I/O constraints of an Arduino 
  Nano circuit board.  It generates either CW 5 to 100 WPM Morse code, 
  or FSK Baudot hardline signals of 45.45, 50 or 75 Baud.

  Copyright (C) 2018 David Freese W1HKJ
  Derived from tinyFSK by Andrew T. Flowers K0SM

  nanoIO is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with tinyCWFSK.ino.  If not, see <http://www.gnu.org/licenses/>.

  Revisions:

 //1.0.0:  Initial release
 //1.0.1:  MORTTY Version 3 board support
 //1.1.0:  added support for WPM potentiometer; wiper on A0
 //1.3.0:  added additional FSK echo of control strings
 //1.3.1:  enabled keyline inputs
 //1.4.0:  MORTTY Version 4 board support
 //1.4.1:  Modified usec corr usage
 //1.4.1-NI2O : Added NI2O board


***********************************************************************/

#include "TimerOne.h"
#include "Morse.h"
#include "Keyer.h"

#include "EEPROM.h"
#include "constants.h"
#include "ascii_map.h"

#include "config.h"

/******************************************************
     Variable declarations
*******************************************************/

/*******************************************************
  This section defines static runtime variables that affect
  RTTY transmission.  They are NOT directly changeable by user
  commands because they can get ops into trouble.  They are
  here for the tinkerer/experimenter in case you want access
  to them at runtime.
********************************************************/

long serialSpeed = 9600; //This is the speed for the serial
//(more likely USB) connection, 8-N-1

// Not user selectable, but USOS behavior can be changed here.
// We set this to TX extra shifts to be compatible with silly
// MMTTY default reciever
int usos = USOS_MMTTY_HACK;

int stopBits =  STOP_BITS_1R5;  // TX 1.5 stop bits

/***************************************
  Dynamic runtime variables these are minipulated with
  user commands or during normal TX operation.
*****************************************/
float baudrate = 45.45;  //default--can be changed by user command

int pttLeadMillis = 150; //time before first start bit
int pttTailMillis = 25;  //time after last stop bit


// Polarity--changed with user commands and stored in EEPROM
boolean mark = LOW;     //High indicates +V on the FSK/CW pin
boolean space = HIGH;   //Low indicates 0V on the FSK/CW pin

// Buffer management variables to handle TX text input
byte sendBufferArray[SEND_BUFFER_SIZE];  // size of TX buffer
byte sendBufferBytes = 0;    // number of bytes unsent in TX buffer
byte lastAsciiByteSent = 0;  // needed to echo back sent characters to terminal
boolean endWhenBufferEmpty = true;  //flag to kill TX when buffer empty (']')


byte currentShiftState = SHIFT_UNKNOWN;  //Keeps track of Letter/Figs state to determine
//if we need to send shift chars

boolean ptt = false; // Keeps track of PTT state (true = Transmitter is on)

volatile boolean isrFlag = false;   //set by timer interrupt.  Set high every 1/2 bit
//to indicate when we should exectute the bit-banging
//routine.  This is handled in the main loop function.

boolean configurationMode = false;  //flag indicates if we are in the menu system or
//in normal operation.

char mode = DEFAULT_MODE;
//----------------------------------------------------------------------
// CW variables

CWSTRUC CWstruc {
  18,      // int   cw_wpm;
  300,     // int   weight;
  2,       // int   incr;
  18,      // int   key_wpm;
  0,       // int   corr_usec
  IAMBICA, // int   keyer_mode;
  1,       // int   ptt_enable;
  10,      // int   pot min value;
  20       // int   pot range value;
};

boolean pot_control = true;
int     spd_cmd = 18;
float   wt_cmd = 3.0;

int     value = 0;
boolean value_string = false;
boolean pot_string = false;
boolean ptt_string = false;
boolean incr_char = false;

Morse morse(CWstruc.cw_wpm, CWstruc.weight/100.0);
Keyer keyer(CWstruc.key_wpm, CWstruc.weight/100.0);

//----------------------------------------------------------------------

/*********************************************************************
  Main execution
***********************************************************************/

/**
  Exectutes *once* at program start (when power applied or
  reset button pressed.  Note that many Arudino devices have a
  "software reset" option that will reset the processor when
  the serial port is opened.
  It opens the port, configures the output pins, and loads
  configuration from EEPROM.
*/

void setup()
{
// configure pins for output
  pinMode(FSK_PIN, OUTPUT);
  pinMode(PTT_PIN, OUTPUT);
  pinMode(CW_PIN, OUTPUT);

  eeLoad();

  morse.wpm(CWstruc.cw_wpm);
  morse.weight(CWstruc.weight/100.0);
  keyer.wpm(CWstruc.key_wpm);
  keyer.set_mode(CWstruc.keyer_mode);

  Serial.begin(serialSpeed);

// start the half-bit timer.
  initTimer();

//  resetSendBuffer();
  sendBufferBytes = 0;

//  morse.send('E', CW_PIN);

// Tell N1MM we are in "RX" mode.
  Serial.write("\ncmd:\n");

  morse.send('T', CW_PIN);
}

/**
  Main loop.  This loop does two things:
  (1) Process any input from the serial connection one byte at a time.

  (2) If the half-bit timer interrupt fired we need to execute the bit-banging
  routine to keep clocking out RTTY.
*/

void do_serial()
{
// Read up to SEND_BUFFER_SIZE characters from the USB serial port

  while ((Serial.available() > 0) && (sendBufferBytes < SEND_BUFFER_SIZE)) {

// get incoming byte:
    byte b = Serial.read();

// Process configuration string if we are in configuration mode
    if (configurationMode) {
      echo(b);
      handleConfigurationCommand(b);
    }
    else  switch (b) {
// test for configuration mode character
      case COMMAND_ESCAPE :
        configurationMode = true;
        echo(b);
        break;
// check for TX abort character.  This immediately kills the
// transmitter and dumps anything remaining in the buffer.
      case TX_ABORT : 
        echo(b);
        setPTT(false);
        resetSendBuffer();
        endWhenBufferEmpty = true;
        break;
      case TX_ON : 
        echo(b);
        endWhenBufferEmpty = false;
        setPTT(true);
        break;
      case TX_END : 
        echo(b);
        endWhenBufferEmpty = true;
        break;
      default :
// add character (b) to send buffer
        if (b == 0) break;
        if (mode == FSK_MODE)
          addToSendBuffer(b);
        else {
          if (b < ' ') {
            addToSendBuffer(' ');
          } else {
            addToSendBuffer(b);
          }
        }
      }
  }  // end while (Serial.available...)

// If the ISR fired we need may need to bit-bang something out the FSK port
  if (mode == FSK_MODE) {
    if (isrFlag) {
      processHalfBit();
      isrFlag = false;
    } else
      isrFlag = false;
  }
  else { // mode is CW_MODE
    if (sendBufferBytes > 0) {
      send_next_CW_char();
    } else if (endWhenBufferEmpty) {
      setPTT(false);
      endWhenBufferEmpty = false;
    }
  }
}

void send_wpm()
{
  Serial.write("\nwpm "); Serial.println(CWstruc.key_wpm);
}


void send_ptt_state()
{
  Serial.write("ptt ");
  if (CWstruc.ptt_enable) Serial.write(" enabled\n");
  else Serial.write(" disabled");
}
  
// Read A0, wiper arm of speed pot, 0 ... 5 v
// convert to wpm MIN ... MAX

void read_pot_wpm()  
{
#ifdef WITH_SPEED_POT
  int pot_wpm = CWstruc.min_pot_wpm + CWstruc.rng_pot_wpm * analogRead(WPM_POT) / 1023;

  if ((pot_control == 1) && (pot_wpm != CWstruc.cw_wpm)) {
    CWstruc.key_wpm = pot_wpm;
    keyer.wpm(CWstruc.key_wpm);
  }
#endif
}

void loop()
{
  if (mode == CW_MODE) {
    read_pot_wpm();
    if (!keyer.do_paddles()) 
      do_serial();
  } else
    do_serial(); 
}

// Handle configuration change commands by changing variables
// and writing new values to EEPROM.
//
// Configuration change commands are preceded by the ~ character
//
// ~A, ~a - IambicA keying
// ~B, ~b - IambicB keying
// ~K, ~k - Straight Key
// ~C, ~c - change to CW output
// ~E, ~e - timing error corr in usec
// ~F, ~f - change to FSK output
// ~P1    - CW WPM Pot control ON
// ~P0    - CW WPM Pot control OFF
// ~P?    - CW WPM reading
// ~X0    - disable PTT
// ~X1    - enable PTT
// ~X?    - PTT state
// ~T, ~t - enable CW tune position (] terminates)
// ~Snnns - change CW WPM to nnn
// ~Unnnu - change CW keyer WPM to nnn
// ~Dnnnd - change CW dash/dot ratio to nnn/100
// ~In    - change CW incr/decr value (1...9)
// ~0     - Set FSK mark = HIGH
// ~1     - Set FSK mark = LOW
// ~4     - Set FSK baud to 45.45
// ~5     - Set FSK baud to 50.0
// ~7     - Set FSK baud to 75.0
// ~9     - Set FSK baud to 100.0
// ~?     - Report current configuration
// ~W     - Save config to EEPROM
// ~~     - Show command set

void handleConfigurationCommand(byte b)
{
  if (value_string && b >= '0' && b <= '9') {
    value = value * 10 + b - '0';
    return;
  }
  if (pot_string) {
    if (b == '1') pot_control = 1;
    else if (b == '0') pot_control = 0;
    else if (b == '?') send_wpm();
    pot_string = false;
    configurationMode = false;
    return;
  }
  if (ptt_string) {
    if (b == '1') CWstruc.ptt_enable = 1;
    else if (b == '0') CWstruc.ptt_enable = 0;
    else if (b == '?') send_ptt_state(); 
    ptt_string = false;
    configurationMode = false;
    return;
  }
  if (incr_char) {
    int val = b - '0';
    if (val > 0 && val < 10) CWstruc.incr = val;
    incr_char = false;
  }

  switch (b) {
    case 'A' : case 'a' :
        CWstruc.keyer_mode = IAMBICA;
        keyer.set_mode(IAMBICA);
        configurationMode = false;
        break;
    case 'B' : case 'b' :
        CWstruc.keyer_mode = IAMBICB;
        keyer.set_mode(IAMBICB);
        configurationMode = false;
        break;
    case 'K' : case 'k':
        CWstruc.keyer_mode = STRAIGHT;
        keyer.set_mode(STRAIGHT);
        configurationMode = false;
        break;
    case 'C' : case 'c' :
        mode = CW_MODE;
        configurationMode = false;
        break;
    case 'F' : case 'f' :
        mode = FSK_MODE;
        configurationMode = false;
        break;
    case 'P' : case 'p' :
        pot_string = true;
        break;
    case 'X': case 'x' :
        ptt_string = true;
        break;
    case 'T' : case 't' :
        enable_tune();
        configurationMode = false;
        break;
    case COMMAND_POLARITY_MARK_HIGH :
        mark = HIGH;
        space = LOW;
        EEPROM.write(EE_POLARITY_ADDR, b);
        configurationMode = false;
        break;
    case COMMAND_POLARITY_MARK_LOW :
        mark = LOW;
        space = HIGH;
        EEPROM.write(EE_POLARITY_ADDR, b);
        configurationMode = false;
        break;
    case COMMAND_45BAUD :
        baudrate = 45.45;
        initTimer();
        EEPROM.write(EE_SPEED_ADDR, b);
        configurationMode = false;
        break;
    case COMMAND_50BAUD :
        baudrate = 50.0;
        initTimer();
        EEPROM.write(EE_SPEED_ADDR, b);
        configurationMode = false;
        break;
    case COMMAND_75BAUD :
        baudrate = 75.0;
        initTimer();
        EEPROM.write(EE_SPEED_ADDR, b);
        configurationMode = false;
        break;
    case COMMAND_100BAUD :
        baudrate = 100.0;
        initTimer();
        EEPROM.write(EE_SPEED_ADDR, b);
        configurationMode = false;
        break;
    case 'D' : // start dash/dot ratio
        value_string = true;
        value = 0;
        return;
    case 'd' : // end dash/dot ratio
        value_string = false;
        if (value >= 250 && value <= 350) {
          CWstruc.weight = value;
          morse.weight(CWstruc.weight/100.0);
        }
        configurationMode = false;
        break;
    case 'E' : // start timing error usec
        value_string = true;
        value = 0;
        return;
    case 'e' : // end dash/dot ratio
        value_string = false;
        CWstruc.corr_usec = value;
        configurationMode = false;
        break;
    case 'I' : case 'i' : // incr/dec value
        incr_char = true;
        return;
    case 'S' : // start Speed (wpm)
        value_string = true;
        value = 0;
        return;
    case 's' : // end speed
        value_string = false;
        if (value >= MIN_CW_WPM && value <= MAX_CW_WPM) {
          CWstruc.cw_wpm = value;
          morse.wpm(CWstruc.cw_wpm);
        }
        configurationMode = false;
        break;
    case 'U' : // start key wpm (user)
        value_string = true;
        value = 0;
        return;
    case 'u' : // end speed
        value_string = false;
        if (value >= MIN_CW_WPM && value <= MAX_CW_WPM) {
          CWstruc.key_wpm = value;
          keyer.wpm(CWstruc.key_wpm);
        }
        configurationMode = false;
        break;

    case 'M' : // start pot min wpm 
        value_string = true;
        value = 0;
        return;
    case 'm' : // end pot min wpm
        value_string = false;
        CWstruc.min_pot_wpm = value;
        configurationMode = false;
        break;

    case 'N' : // start pot range wpm 
        value_string = true;
        value = 0;
        return;
    case 'n' : // end pot min wpm
        value_string = false;
        CWstruc.rng_pot_wpm = value;
        configurationMode = false;
        break;

    case COMMAND_DUMP_CONFIG :
        displayConfiguration();
        configurationMode = false;
        break;
    case 'W' : case 'w' :
      eeSave();
      configurationMode = false;
      break;
    case COMMAND_ESCAPE :
      displayConfigurationPrompt();
      configurationMode = false;
      break;
    default :
      value_string = false;
      configurationMode = false;
      pot_string = false;
      ptt_string = false;
      Serial.write("\nCommand?\n");
  }
}

/**
  Loads speed and polarity from EEPROM
*/
void eeLoad()
{
  mode           = EEPROM.read(EE_MODE);
  if (mode != CW_MODE && mode != FSK_MODE) mode = CW_MODE;
  byte speedChar = EEPROM.read(EE_SPEED_ADDR);
  byte polarity  = EEPROM.read(EE_POLARITY_ADDR);

  if (polarity == COMMAND_POLARITY_MARK_LOW) {
    mark = LOW;
  } else {
    mark = HIGH;
  }
  space = !mark;

  switch (speedChar) {
    case COMMAND_50BAUD :
        baudrate = 50.0;
        break;
    case COMMAND_75BAUD :
        baudrate = 75.0;
        break;
    case COMMAND_100BAUD :
        baudrate = 100.0;
        break;
    default:
        baudrate = 45.45;
        break;
  }

  EEPROM.get(EE_CW_STRUC_ADDR, CWstruc);
  if (CWstruc.cw_wpm < MIN_CW_WPM) CWstruc.cw_wpm = 18;
  if (CWstruc.cw_wpm > MAX_CW_WPM) CWstruc.cw_wpm = 18;
  if (CWstruc.key_wpm < MIN_CW_WPM) CWstruc.key_wpm = 18;
  if (CWstruc.key_wpm > MAX_CW_WPM) CWstruc.key_wpm = 18;
  if (CWstruc.weight < 250) CWstruc.weight = 300;
  if (CWstruc.weight > 350) CWstruc.weight = 300;
  if (CWstruc.ptt_enable < 0 || CWstruc.ptt_enable > 1) CWstruc.ptt_enable = 1;
  if (CWstruc.min_pot_wpm < 5 || CWstruc.min_pot_wpm > 100) CWstruc.min_pot_wpm = 5;
  if (CWstruc.rng_pot_wpm < 10 || CWstruc.rng_pot_wpm > 30) CWstruc.rng_pot_wpm = 20;
  if (CWstruc.incr < 1) CWstruc.incr = 2;
  if (CWstruc.incr > 9) CWstruc.incr = 2;
//  eeSave();
}

void eeSave()
{
  EEPROM.write(EE_MODE, mode);
  if (baudrate == 45.45) EEPROM.write(EE_SPEED_ADDR, COMMAND_45BAUD);
  if (baudrate == 50.0)  EEPROM.write(EE_SPEED_ADDR, COMMAND_50BAUD);
  if (baudrate == 75.0)  EEPROM.write(EE_SPEED_ADDR, COMMAND_75BAUD);
  if (baudrate == 100.0) EEPROM.write(EE_SPEED_ADDR, COMMAND_100BAUD);
  if (mark == LOW) EEPROM.write(EE_POLARITY_ADDR, COMMAND_POLARITY_MARK_LOW);
  else             EEPROM.write(EE_POLARITY_ADDR, COMMAND_POLARITY_MARK_HIGH);

  EEPROM.put(EE_CW_STRUC_ADDR, CWstruc);
}

/**
  Init the timer to fire every *half* bit period.  This allows us
  to have 1.5 stop bits if we want.
*/
void initTimer()
{
  Timer1.stop();
  long bitPeriod = (long) ((1.0f / baudrate) * 1000000); //micros
  Timer1.initialize(bitPeriod / 2.0);
  Timer1.attachInterrupt(timerISR);
}

/**
  The ISR for the half-bit timer is just to set a flag.  We
  will process in the main loop.
*/
void timerISR()
{
  isrFlag = true;
}

/**
  Displays the configuration options on the console
*/
void displayConfigurationPrompt()
{
Serial.write("\
\nCmd ~...\
\nC,c   CW\
\nF,f   FSK\
\nT,t   Tune\
\nSvvvs comp wpm\
\nUvvvu key wpm\
\nDvvvd dash/dot");
 #ifdef WITH_SPEED_POT
Serial.write("\
\nP0    pot disabled\
\nP1    pot enabled\
\nP?    pot wpm\
\nMvvvm pot min\
\nNvvvn pot range");
 #endif
Serial.write("\
\nX0    ptt ON\
\nX1    ptt OFF\
\nX?    ptt state");
Serial.write("\
\nIn    incr\
\nA,a   IambicA\
\nB,b   IambicB\
\nK,k   Straight\
\n0     mark HIGH\
\n1     mark LOW\
\n4     45.45 baud\
\n5     50 baud\
\n7     75 baud\
\n9     100 baud\
\n?     Show config\
\nW     Write EEPROM\
\n~     Show cmds\n");
}

/**
  Prints the current configuration the console
*/
void displayConfiguration()
{
  Serial.write("\nnanoIO ver ");
  Serial.write(VERSION);
  Serial.write("\nMode ");
  if (mode == FSK_MODE)
    Serial.write("FSK");
  else
    Serial.write("CW");
  Serial.write("\nFSK");
  Serial.write("\n  Baud "); Serial.print(baudrate);
  Serial.write("\n  Mark ");
  if (mark == LOW) {
    Serial.write("LOW");
  } else {
    Serial.write("HIGH");
  }
  Serial.write("\nCW");
  Serial.write("\n      PC WPM "); Serial.print(CWstruc.cw_wpm);
  Serial.write("\n  Paddle WPM "); Serial.print(CWstruc.key_wpm);
  Serial.write("\n   corr usec "); Serial.print(CWstruc.corr_usec);
  Serial.write("\n    dash/dot "); Serial.print(CWstruc.weight/100.0);
  Serial.write("\n        incr "); Serial.print(CWstruc.incr);
  Serial.write("\n  Keyer mode ");
  if (keyer.get_mode() == STRAIGHT) Serial.write("Straight");
  else if (keyer.get_mode() == IAMBICA) Serial.write("IambicA");
  else Serial.write("IambicB");
#ifdef WITH_SPEED_POT
  Serial.write("\n   Speed Pot "); Serial.print(pot_control ? "ON" : "OFF");
  Serial.write("\n     minimum "); Serial.print(CWstruc.min_pot_wpm);
  Serial.write("\n       range "); Serial.print(CWstruc.rng_pot_wpm);
#endif
  Serial.write("\nPTT "); Serial.write(CWstruc.ptt_enable ? "YES" : "NO");
  Serial.write("\n");
}

/******************************************************************
   handle CW characters in buffer
*/
void send_next_CW_char()
{
  if (sendBufferBytes > 0) {
    byte chr = sendBufferArray[0];
    for (int i = 0; i < sendBufferBytes - 1; i++) {
      sendBufferArray[i] = sendBufferArray[i + 1];
    }
    sendBufferBytes--;
    if (chr == '^') {
      CWstruc.cw_wpm += CWstruc.incr;
      if (CWstruc.cw_wpm > 100) CWstruc.cw_wpm = 100;
      morse.wpm(CWstruc.cw_wpm);
      return;
    }
    if (chr == '|') {
      CWstruc.cw_wpm -= CWstruc.incr;
      if (CWstruc.cw_wpm < 5) CWstruc.cw_wpm = 5;
      morse.wpm(CWstruc.cw_wpm);
      return;
    }
    morse.send(chr, CW_PIN);
    echo(chr);
  }
}

/******************************************************************
  This called every half-bit period to figure out what to bit-bang
  out the FSK pin.  It is basically an incremental counter that
  counts half bit periods and toggles the bits of the baudot character
  as needed.  It bangs out the start bit, five symbol bits, and the
  stop bit, which is 1.5 bits long (hence the need to have a timer
  counting half bits).
  The 5 bit RTTY character frame looks like this:

        ||Start | LSB |  X  |  X  |  X  | MSB | Stop    ||
  bitPos:   -1     0     1     2     3     4      5
******************************************************************/
int sendingChar = LTRS_SHIFT; // default--this is the "diddle character"
int stopBitCounter = 0; // counts half-bits for stop bit
int bitPos = START_BIT_POS;        // -1 = Start bit
bool midBit = false;    // used as like an ignore flag--we usually don't
// toggle state in the middle of a bit.  The exception
// is the stop bit, which is often 1.5 bits long.
void processHalfBit() {

  if (!ptt)  //not transmitting, so just return--there's nothing to send.
    return;

  if (midBit) {
    midBit = false; // reset the flag.  Next time we need to send the next bit.
    return;
  }

  // it's time to bang out the next bit.  We check for the special cases
  // first.  If it's a start bit we always sent SPACE and if its a STOP bit
  // we always send MARK.
  if (bitPos == START_BIT_POS) {  // we have to send a start bit

// If it is time to send a start bit, we grab the next character to send so
// that it is ready the next time through the loop.  The next character
// might be the TX_END_FLAG, in which case we need to turn off the transmitter.
    sendingChar = getNextSendChar();

    if (ptt) {
      if (sendingChar == TX_END_FLAG) { //end of data to send
        setPTT(false);
        return;
      } else {
        digitalWrite(FSK_PIN, space);  //start bit is always space
        bitPos++;
        midBit = true;
      }
    }
  }
  else if (bitPos == STOP_BIT_POS) { // we have to send a stop bit
    if (stopBitCounter == 0) {
      digitalWrite(FSK_PIN, mark);
      stopBitCounter = stopBits;  //this determines # of half-bit periods we stay in stop bit
    } else { // already in stop bit, just decrement
// stopBitCounter counts half-bit periods.  2 ==> one stop bit
//                                          3 ==> 1.5 stop bits
//                                          4 ==> two stop bits
      stopBitCounter--;
      if (stopBitCounter == 0) { // end of stop bit period
        bitPos = START_BIT_POS;  // move on to start bit of next char

//  If we just sent an explicit LTRS or FIGS shift, obviously we are in that state.  If USOS is turned on and we
//  have sent a space character, we are implicitly in LTRS shift.
        if (sendingChar == LTRS_SHIFT || (usos == USOS_ON && sendingChar == 0x04) ) { //0x04 = Baudot space
          currentShiftState = LTRS_SHIFT;
        } else if (sendingChar == FIGS_SHIFT) {
          currentShiftState = FIGS_SHIFT;
        } else {
			echo(lastAsciiByteSent);
		}
      }
    }
  } else {
// We are not sending a stop/start bit, so we send the next bit of the of the character.
    bool b = (sendingChar & (0x01 << bitPos));  //LSB first
    if (b) {
      digitalWrite(FSK_PIN, mark);
    }
    else {
      digitalWrite(FSK_PIN, space);
    }
    bitPos++;
    midBit = true;
  }
}

/**
  Reset character buffer.  This is a helper routine when stop the
  transmitter so that everything is back to initial states ready
  to bang out the first character.
*/
void resetChar()
{
  sendingChar = LTRS_SHIFT;
  stopBitCounter = 0;
  bitPos = START_BIT_POS;
  midBit = false;
}

/**
  Wipes the send buffer. Helper function for aborting
  a transmission.
*/
void resetSendBuffer()
{
  for (int i = 0; i < SEND_BUFFER_SIZE; i++)
    sendBufferArray[i] = 0;
  sendBufferBytes = 0;
}

/**
  Adds a new byte to the transmit text buffer.  These
  are *ASCII* bytes from the terminal, not Baudot.
*/
void addToSendBuffer(byte newByte)
{
  if (sendBufferBytes < SEND_BUFFER_SIZE - 1) {
    sendBufferBytes++;
    sendBufferArray[sendBufferBytes - 1] = newByte;
  }
}

/**
  Gets the next Baudot (5-bit) char from the buffer.  This
  function will return LTRS or FIGS shift characters when
  needed depending on the current shift state and USOS setting.
*/
byte getNextSendChar()
{

  byte rVal = LTRS_SHIFT;  //default "idle" or "diddles" when nothing to send

  if (sendBufferBytes > 0) {  // there is still data in buffer to send
    byte asciiByte = sendBufferArray[0];

    if (currentShiftState != LTRS_SHIFT && requiresLetters(asciiByte)) {
      //echo('_');
      return LTRS_SHIFT;
    }
    else if (currentShiftState != FIGS_SHIFT && requiresFigures(asciiByte)) {
      //echo('^');
      return FIGS_SHIFT;
    }
    // Special "robust" USOS case--send FIGS after a space even if already in FIGS state and next
    // character requires FIGS shift.  Note: when this is called
    // sendingChar is the char we just *finished* sending
    else if ( (usos == USOS_MMTTY_HACK) && 
              (currentShiftState != LTRS_SHIFT) && 
              requiresFigures(asciiByte) && 
              (sendingChar == 0x04) ) {
//echo('^');
      return FIGS_SHIFT;
    }
    else {
//we don't need to send a shift character.  Just find the baudot equiv of the ascii symbol and return it.
      rVal = asciiToBaudot[asciiByte];
      lastAsciiByteSent = asciiByte;
      if (sendBufferBytes > 0) {
        for (int i = 0; i < sendBufferBytes - 1; i++) {
          sendBufferArray[i] = sendBufferArray[i + 1];
        }
      sendBufferBytes--;
      }
//      echo(asciiByte);
    }
  }
  else {
// the buffer is empty
    if (endWhenBufferEmpty) {
      rVal = TX_END_FLAG;  // signals to stop the TX
    } else {
// slow typist?
      if (currentShiftState == SHIFT_UNKNOWN) {
        rVal = LTRS_SHIFT;  //send LTRS idle if we haven't sent anything on this TX
      }
      else {
        rVal = currentShiftState; // idle on LTRS or FIGS depending on what state we are in
      }
    }
  }
  return rVal;
}


/*
  returns whether or not this is a "letter".  Letters require LTRS
  shift preceding the byte if currently in FIGS mode.
*/
boolean requiresLetters(byte asciiByte)
{
  return (asciiByte >= 'A' && asciiByte <= 'Z')
         || (asciiByte >= 'a' && asciiByte <= 'z');

}

/**
  Helper function to find out whether a particular byte
  needs the FIGS shift preceeding it.
*/
boolean requiresFigures(byte asciiByte)
{
  return !requiresLetters(asciiByte)
         && (asciiByte != ASCII_NULL) //null
         && (asciiByte != ASCII_LF)   //LF
         && (asciiByte != ASCII_CR)
         && (asciiByte != ' ');
}


/**
  Turns the PTT on or off and applies any delays that might exist.
*/
void setPTT(byte b)
{

  if (b)
  { // PTT ON
    if (mode == FSK_MODE) {
      digitalWrite(FSK_PIN, mark);  //always start in mark state
      digitalWrite(PTT_PIN, HIGH);
      // we will stay in the mark state for some amount of time
      // before sending the first start bit of the first character
      delay(pttLeadMillis);
    } else {
      if (CWstruc.ptt_enable)
        digitalWrite(PTT_PIN, HIGH);
    }
  }
  else
  { // PTT OFF
    if (mode == FSK_MODE) {
      digitalWrite(PTT_PIN, LOW); // drop PTT
      digitalWrite(FSK_PIN, space);
      digitalWrite(CW_PIN, LOW);
      delay (pttTailMillis);
      stopBitCounter = 0;
      bitPos = -1;
      currentShiftState = SHIFT_UNKNOWN;
      lastAsciiByteSent = 0;
    } else {
      digitalWrite(PTT_PIN, LOW);
      digitalWrite(CW_PIN, LOW);
    }
#ifdef N1MM
    if (mode == FSK_MODE) 
      Serial.write("\ncmd:\n"); // Tells N1MM that TX is finished
#endif
  }
  ptt = b;
}

void enable_tune()
{
  digitalWrite(PTT_PIN, HIGH);
  digitalWrite(CW_PIN, HIGH);
}

/**
  Echo to the serial port.  This will show up in the user's terminal
  if he or she is watching.
*/
void echo(byte b)
{
  Serial.write(b);
}
