# nanoIO
nanoIO CW/RTTY keyer for Arduino Nano

![finished board](https://github.com/g7ltt/nanoIO/blob/main/PXL_20210929_022402144.jpg)

My take on W1HK's nanoIO CW/RTTY keyer for FLDigi.

https://oshpark.com/shared_projects/0wyCgFsd

Mount the board component side down on the bottom of the Arduino Nano.


NanoIO Version 1.3.1
Authors:
David Freese, W1HKJ
Ervin Hegedüs, HA2OS

An FSK / CW interface based on the Arduino nano. Sketch includes an FSK modulator, a CW computer interface, and a CW iambic-A/B keyer.

FSK Specifications
5 bit Baudot
baud rates 45.45, 50, 75 and 100
CW Specifications:
5 to 100 WPM
dash/dot ratio adjustable 2.5 to 3.5
in-line increment decrement WPM using ^ and | characters
incremental size user adjustable
PTT signal generated by Arduino

Both an internal buffer of 200 characters is available for buffered transmit.

#Hardware requirement
Arduino nano or compatible (author used nano from Elegoo)
LTV-827 dual opto-isolator
2 620 ohm ¼ watt resistor (should work with 500 < R < 820 ohm)
suitable connectors to interface to transceiver

Default pin assignments defined in Arduino Sketch for the above circuit.

D9 / PIN 9 – spare
D10 / PIN 10 – PTT
D11 / PIN 11 - FSK_PIN
D12 / PIN 12 – CW

Default startup configuration
nanoIO 1.0.1
FSK: Baud: 45.45, Mark LOW
CW: WPM: 18/18, dash/dot 3.00, incr 2, IambicA keyer

Computer Interface Command Set
USB serial command strings all begin with the tilde, ~, character

Cmd ~...

C,c CW mode
F,f FSK mode
T,t CW Tune
Snnns computer wpm 10...100
Unnnu key (user) wpm 10...100
Dnnnd dash/dot 250...350 (2.5...3.5)
In CW incr (1..9)
A,a IambicA
B,b IambicB
K,k Straight key
0 FSK mark = HIGH
1 FSK mark = LOW
4 45.45 baud
5 50 baud
7 75 baud
9 100 baud
? Show config
W Write EEPROM
~ Show cmds
In line special characters:
Both:

[ PTT on
] PTT off
\ escape; clear internal buffer and set PTT off
CW:

^ increase computer wpm by increment value (default is 2) *
| decrease computer wpm by increment value *
% <SK>
& <AS>
+ <KN>
- <BT>
< <AS>
= <BT>
> <AR>
{ left brace <HM>
} right brace <VE>
* - the ^| wpm modifiers do not effect the paddle CW, just the computer CW.

for example, a CW string might contain:

~C~S24s[tu ^^5nn|| k]
Mode is changed to CW
Computer WPM is set to 24
PTT is set ON
The string “tu “ is sent
Computer WPM is increased by 2 increment factors
The string “5nn” is sent
Computer WPM is decreased by 2 increment factors
The string “ k” is sent
PTT is set OFF
PTT will enable before starting the CW transmission. PTT will disable after the last CW character (k) is completed.



