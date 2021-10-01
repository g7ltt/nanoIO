//**********************************************************************
//
// Morse, a part of nanoIO
//
// Copyright (C) 2018, David Freese, W1HKJ
//
// nanoIO is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// nanoIO is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with fldigi.  If not, see <http://www.gnu.org/licenses/>.
//
//Revisions:
//
//1.0.0:  Initial release
//1.0.1:  MORTTY Version 3 board support
//1.1.0:  added support for WPM potentiometer; wiper on A0
//
//**********************************************************************

#include "Arduino.h"
#include "Morse.h"
#include "Keyer.h"
#include "config.h"
#include "constants.h"

// Morse conversion table from ASCII (offset by 33);
// code is reverse binary for send method
// terminated with a leftmost 1, i.e. A .- = 0b00000110
// Entry with value 0b000000001 has no morse equivalent

const  byte _ascii_to_morse[] = {
	0b01110101,  // !
	0b01010010,  // "
	0b00000001,  // #
	0b11001000,  // $
	0b01101000,  // % <SK>
	0b00011010,  // & <AS>
	0b01011110,  // '
	0b00101101,  // (  
	0b01101101,  // )
	0b00000001,  // *
	0b00101101,  // + <KN>
	0b01110011,  // ,
	0b00110001,  // - <BT>
	0b00101010,  // .
	0b00101001,  // /
	0b00111111,  // 0
	0b00111110,  // 1
	0b00111100,  // 2
	0b00111000,  // 3
	0b00110000,  // 4
	0b00100000,  // 5
	0b00100001,  // 6
	0b00100011,  // 7
	0b00100111,  // 8
	0b00101111,  // 9
	0b01000111,  // :
	0b01010101,  // ;
	0b00100010,  // <  <AS>
	0b00110001,  // =  <BT>
	0b00101010,  // >  <AR>
	0b01001100,  // ?
	0b01010110,  // @
	0b00000110,  // A
	0b00010001,  // B
	0b00010101,  // C
	0b00001001,  // D
	0b00000010,  // E
	0b00010100,  // F
	0b00001011,  // G
	0b00010000,  // H
	0b00000100,  // I
	0b00011110,  // J
	0b00001101,  // K
	0b00010010,  // L
	0b00000111,  // M
	0b00000101,  // N
	0b00001111,  // O
	0b00010110,  // P
	0b00011011,  // Q
	0b00001010,  // R
	0b00001000,  // S
	0b00000011,  // T
	0b00001100,  // U
	0b00011000,  // V
	0b00001110,  // W
	0b00011001,  // X
	0b00011101,  // Y
	0b00010011,  // Z
	0b00000001,  // [
	0b01000000,  // backslash 6 dits
	0b00000001,  // ]
	0b00000001,  // ^
	0b01101100,  // _
	0b01011110,  // `
	0b00000110,  // a
	0b00010001,  // b
	0b00010101,  // c
	0b00001001,  // d
	0b00000010,  // e
	0b00010100,  // f
	0b00001011,  // g
	0b00010000,  // h
	0b00000100,  // i
	0b00011110,  // j
	0b00001101,  // k
	0b00010010,  // l
	0b00000111,  // m
	0b00000101,  // n
	0b00001111,  // o
	0b00010110,  // p
	0b00011011,  // q
	0b00001010,  // r
	0b00001000,  // s
	0b00000011,  // t
	0b00001100,  // u
	0b00011000,  // v
	0b00001110,  // w
	0b00011001,  // x
	0b00011101,  // y
	0b00010011,  // z
	0b01110000,  // left brace  <HM> ....--
	0b00000001,  // vertical bar
	0b00101000,  // right brace <VE> ...-.
	0b00000001,  // tilde
};

Morse::Morse(int wpm, float weight)
{
	// Save values for later use
	_speed = wpm;
	_wt = weight;
	calc_ratio();
}

void Morse::calc_ratio()
{
  float w = (_wt + 1) / (_wt -1);

  _space_len  = 1200000L / _speed;
  _space_usec = _space_len % 1000;
  _space_msec = _space_len / 1000;

  _dot_len = _space_len * (w - 1);
  _dot_usec = _dot_len % 1000;
  _dot_msec = _dot_len / 1000;

  _dash_len =  _space_len * (w + 1);
  _dash_usec = _dash_len % 1000;
  _dash_msec = _dash_len / 1000;

  _ltr_len  = _space_len * 2;
  _ltr_usec = _ltr_len % 1000;
  _ltr_msec = _ltr_len / 1000;

  _comp_len = _space_len;
  if (_comp_len > 50 * CWstruc.corr_usec)
    _comp_len -= 50 * CWstruc.corr_usec;
  _comp_usec = _comp_len % 1000;
  _comp_msec = _comp_len / 1000;
   
}

void Morse::weight(float wt)
{
	_wt = wt;
	calc_ratio();
}

void Morse::wpm(int spd)
{
	_speed = spd;
	calc_ratio();
}

void Morse::dash(byte pin)
{
  digitalWrite(pin, HIGH);
  delay(_dash_msec);
  if (_dash_usec > 0) delayMicroseconds(_dash_usec);

	digitalWrite(pin, LOW);
  delay(_space_msec);
	if (_space_usec > 0) delayMicroseconds(_space_usec);
 
}

void Morse::dit(byte pin)
{
  digitalWrite(pin, HIGH);
  delay(_dot_msec);
  if (_dot_usec > 0) delayMicroseconds(_dot_usec);

  digitalWrite(pin, LOW);
  delay(_space_msec);
  if (_space_usec > 0) delayMicroseconds(_space_usec);
}

char lastc = 0;

void Morse::send(char c, byte pin)
{
	byte _i;
	byte _p;

	// Send space
	if (c == ' ') {
    int spc = 3;
		if (lastc == ' ') spc = 6;
    delay(spc * _space_msec);
    if (_space_usec > 0) delayMicroseconds(spc * _space_usec);
    delay(_comp_msec);
    if (_comp_usec > 0) delayMicroseconds(_comp_usec);
		return;
	}

	// Do a table lookup to get morse data
	else {
		_i = ((byte) c) - 33;
		_p = _ascii_to_morse[_i];
	}

	// Main algorithm for each morse sign
  if (CWstruc.ptt_enable)
    digitalWrite(PTT_PIN, HIGH); // PTT ON
	while (_p != 1) {
		if (_p & 1)
			dash(pin);
		else
			dit(pin);
		_p = _p / 2;
	}
  digitalWrite(PTT_PIN, LOW);   // PTT OFF
	// Letterspace
  delay(_ltr_msec);
	if (_ltr_usec > 0) delayMicroseconds(_ltr_usec);
}
