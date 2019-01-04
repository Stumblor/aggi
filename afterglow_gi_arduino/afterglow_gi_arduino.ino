/***********************************************************************
 *  Afterglow GI:
 *      Copyright (c) 2019 Christoph Schmid
 *
 ***********************************************************************
 *  This file is part of the Afterglow GI pinball LED project:
 *  https://github.com/smyp/afterglow_gi
 *
 *  Afterglow GI is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, either version 3 of the
 *  License, or (at your option) any later version.
 *
 *  Afterglow GI is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with Afterglow GI.
 *  If not, see <http://www.gnu.org/licenses/>.
 ***********************************************************************/
 
//------------------------------------------------------------------------------
/* This code assumes following pin layout:
 *
 *  +----------+---------------+-----------+---------------+--------------+
 *  | Name     | Function      | Nano Pin# | Register, Bit | Mode         |
 *  +----------+---------------+-----------+---------------+--------------+
 *  | STR1_IN  | D0 Data Bus   | D2        | DDRD, 2       | Input        |
 *  | STR2_IN  | D1 Data Bus   | D3        | DDRD, 3       | Input        |
 *  | STR3_IN  | D2 Data Bus   | D4        | DDRD, 4       | Input        |
 *  | STR4_IN  | D3 Data Bus   | D5        | DDRD, 5       | Input        |
 *  | STR5_IN  | D4 Data Bus   | D6        | DDRD, 6       | Input        |
 *  | ZC       | Zero Crossing | D7        | DDRD, 7       | Input        |
 *  | STR1_OUT | STR 1 Enable  | D8        | DDRB, 0       | Output       |
 *  | STR2_OUT | STR 2 Enable  | D9        | DDRB, 1       | Output       |
 *  | STR3_OUT | STR 3 Enable  | D10       | DDRB, 2       | Output       |
 *  | STR4_OUT | STR 4 Enable  | D11       | DDRB, 3       | Output       |
 *  | STR5_OUT | STR 5 Enable  | D12       | DDRB, 4       | Output       |
 *  | POT      | Potentiometer | A1        | DDRC, 1       | Input        |
 *  +----------+---------------+-----------+---------------+--------------+
*/

#include <EEPROM.h>
#include <avr/wdt.h>
#include <avr/boot.h>

//------------------------------------------------------------------------------
// Setup

// Afterglow GI version number
#define AFTERGLOW_GI_VERSION 100

// Afterglow configuration version
#define AFTERGLOW_CFG_VERSION 1

// Afterglow GI board revision. Currently v1.0.
#define BOARD_REV 10

// turn debug output via serial on/off
#define DEBUG_SERIAL 1

// local time interval (us)
#define TTAG_INT 125

// PWM frequency [Hz]
#define PWM_FREQ 200

// Full PWM cycle (us)
#define PWM_FULL_CYCLE (1000000UL / PWM_FREQ)

// Number of PWM steps in a full cycle
#define PWM_NUM_STEPS (PWM_FULL_CYCLE / TTAG_INT)

// Number of GI strings
#define NUM_STRINGS 5

// Number of brightness steps
#define NUM_BRIGHTNESS 7


//------------------------------------------------------------------------------
// global variables

// local time
static volatile uint32_t sTtag = 0;

static byte sLastPIND = 0;
static uint32_t sDataIntLast[NUM_STRINGS] = {0};
static uint8_t sBrightnessN[NUM_STRINGS] = {0};
static uint32_t sZCIntTime = 0;
static uint8_t sInterruptsSeen = 0;
static volatile uint8_t sBrightness[NUM_STRINGS] = {0};

static uint8_t sCycleTable[PWM_NUM_STEPS] = {0};
static uint8_t sVoltage = 120; // 12V


//------------------------------------------------------------------------------
void setup()
{
    // Use Timer1 to create an interrupt every TTAG_INT us.
    // This will be the heartbeat of our realtime task.
    noInterrupts(); // disable all interrupts
    TCCR1A = 0;
    TCCR1B = 0;
    // set compare match register for TTAG_INT us increments
    // prescaler is at 1, so counting real clock cycles
    OCR1A = (TTAG_INT * 16);  // [16MHz clock cycles]
    // turn on CTC mode
    TCCR1B |= (1 << WGM12);
    // Set CS10 bit so timer runs at clock speed
    TCCR1B |= (1 << CS10);  
    // enable timer compare interrupt
    TIMSK1 |= (1 << OCIE1A);

    // I/O pin setup
    // D2-D7 are inputs
    DDRD = 0;
    // D8-D13 are outputs, pull them low
    DDRB |= B00111111;
    PORTB &= B11100000;

    // activate pin change interrupts on D2-D7
    PCICR |= 0b00000100;
    PCMSK2 |= 0b11111100;
    // clear any outstanding interrupts
    PCIFR = 0;

    // enable serial output at 115200 baudrate
    Serial.begin(115200);
    Serial.print("Afterglow GI v");
    Serial.print(AFTERGLOW_GI_VERSION);
    Serial.println(" (c) 2019 morbid cornflakes");
    // check the extended fuse for brown out detection level
    uint8_t efuse = boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
    Serial.println("-----------------------------------------------");
    uint8_t bodBits = (efuse & 0x7);
    Serial.print("efuse BOD ");
    Serial.println((bodBits == 0x07) ? "OFF" : (bodBits == 0x04) ? "4.3V" : (bodBits == 0x05) ? "2.7V" : "1.8V");

    // populate the cycle table
    populateCycleTable(sVoltage);

    // enable all interrupts
    interrupts();

    // enable a strict 15ms watchdog
    //wdt_enable(WDTO_15MS);
}

//------------------------------------------------------------------------------
// Timer1 interrupt handler
// This is the realtime task heartbeat. All the output magic happens here.
ISR(TIMER1_COMPA_vect)
{
    // Time is ticking
    uint16_t startCnt = TCNT1;
    sTtag++;

    // Update all GI strings
    uint8_t cycle = (sTtag % PWM_NUM_STEPS);
    updateGI(0, cycle, sBrightness[0]);
    updateGI(1, cycle, sBrightness[1]);
    updateGI(2, cycle, sBrightness[2]);
    updateGI(3, cycle, sBrightness[3]);
    updateGI(4, cycle, sBrightness[4]);

    // Kick the dog
    //wdt_reset();

}

//------------------------------------------------------------------------------
// Pin change interrupt on D2-D7 handler
// This is measuring the zero-crossing to blank signal time.
ISR(PCINT2_vect)
{
    // The WPC CPU issues a short signal to turn on the triac controlling the
    // AC voltage. The Triac will stay on until the next zero crossing of the
    // AC signal.
    // The closer the signal is to the zero crossing, the brighter the lamps
    // will be. If no signal is issued at all (stays high), the lamps will light
    // at full power. If the signal remains low, the lamps are turned off.
    // The zero crossing signal is issued with twice the AC frequency, i.e. with
    // 100Hz or every 10ms in Europe.

    // ZC Sig          TR Sig        ZC Sig          ZC     Zero Crossing Signal
    // |                |            |               TR     Triac enable signal
    // |--+  |  |  |  | v|  |  |  |  |--+            B0-B6  Brightness 1-7 levels (WPC GI)
    // |ZC|                          |ZC|
    // |  |B7 B6 B5 B4 B3 B2 B1      |  |
    // +-----------------------------+------
    // 0ms                           10ms

    // check which pin triggered this interrupt
    byte newPins = (sLastPIND ^ PIND);
    sLastPIND = PIND;

    // what time is it?
    uint32_t t = micros();

    if (newPins == B10000000)
    {
        // Handle zero crossing interrupts

        // The zero crossing signal should appear at 100Hz. If we're closer to
        // last interrupt then this is the falling edge and we should ignore it.
        if ((t - sZCIntTime) > 4000)
        {
            // just remember the last zero crossing interrupt time
            sZCIntTime = t;

            // All strings without interrupt in the past interval are either fully
            // on or off
            if ((sInterruptsSeen & 0B00000100) == 0) sBrightness[0] = (PIND & 0B00000100) ? 8 : 0;
            if ((sInterruptsSeen & 0B00001000) == 0) sBrightness[1] = (PIND & 0B00001000) ? 8 : 0;
            if ((sInterruptsSeen & 0B00010000) == 0) sBrightness[2] = (PIND & 0B00010000) ? 8 : 0;
            if ((sInterruptsSeen & 0B00100000) == 0) sBrightness[3] = (PIND & 0B00100000) ? 8 : 0;
            if ((sInterruptsSeen & 0B01000000) == 0) sBrightness[4] = (PIND & 0B01000000) ? 8 : 0;
            /*
            uint8_t pinBit = 0B00000100; // start with D2
            for (uint8_t pinNum=0; pinNum<NUM_STRINGS; pinNum++, pinBit<<=1)
            {
                if ((sInterruptsSeen & pinBit) == 0)
                {
                    sDataIntDt[pinNum] = (PIND & pinBit) ? 0xffffffff : 0;
                }
            }
            */
    
            // Clear the interrupts mask
            sInterruptsSeen = 0;
        }
    }
    else
    {
        // Handle all triggered interrupts
        uint8_t pinBit = 0B00000100; // start with D2
        for (uint8_t pinNum=0; pinNum<NUM_STRINGS; pinNum++, pinBit<<=1)
        {
            // Measure and store the time since the last zero crossing interrupt
            if (newPins & pinBit)
            {
                // Handle only once
                uint32_t dtLast = (t - sDataIntLast[pinNum]);
                if (dtLast > 1000)
                {
                    uint32_t dt = (t - sZCIntTime);
                    if (dt < 10000)
                    {
                        // Translate delta time into brightness
                        sBrightness[pinNum] = dtToBrightness(dt);
                    }
                    sDataIntLast[pinNum] = t;
                    sInterruptsSeen |= pinBit;
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
void loop()
{
    // The main loop is used for low priority serial communication only.
    // All the fun stuff happens in the interrupt handlers.

    // Count the loops (used for debug output below)
    static uint32_t loopCounter = 0;
    loopCounter++;

#if DEBUG_SERIAL
    if ((loopCounter % 10) == 0)
    {
        Serial.print("ZC - ");
        Serial.print(sZCIntTime);
        Serial.println("us");
        for (uint8_t i=0; i<NUM_STRINGS; i++)
        {
            Serial.print(i);
            Serial.print(" - ");
            Serial.println(sBrightness[i]);
        }
	}
#endif

    // wait 500ms
    delay(500);
}

//------------------------------------------------------------------------------
uint8_t dtToBrightness(uint32_t dt)
{
    uint8_t b;
    if (dt < 1000)
    {
        // full power, this shouldn't really happen
        b = 8;
    }
    else if (dt < 2000)
    {
        b = 6;
    }
    else if (dt < 3000)
    {
        b = 5;
    }
    else if (dt < 4000)
    {
        b = 4;
    }
    else if (dt < 5000)
    {
        b = 3;
    }
    else if (dt < 6000)
    {
        b = 2;
    }
    else if (dt < 7000)
    {
        b = 1;
    }
    else
    {
        b = 0;
    }
    return b;
}

//------------------------------------------------------------------------------
void updateGI(uint8_t string, uint8_t cycle, uint8_t brightness)
{
    // Check whether the GI string should be currently lit
    if (brightness >= sCycleTable[cycle])
    {
        // Light the string
        PORTB |= (1 << string);
    }
    else
    {
        // Turn the string off
        PORTB &= ~ (1 << string);
    }
}

//------------------------------------------------------------------------------
void populateCycleTable(uint8_t voltage)
{
    // Assume 6.3V LEDs with 3.2V drop -> 124 Ohm resistor for 25mA current
    uint32_t c = ((uint32_t)voltage - 32)*100/124;
    uint8_t dc = (uint8_t)(25 * 100 / c);
    uint8_t maxDC = (PWM_NUM_STEPS * dc / 100);

    Serial.println("CYCLE TABLE");
    Serial.print("max dc ");
    Serial.print(dc);
    Serial.println("%");
    for (uint8_t i=0; i<PWM_NUM_STEPS; i++)
    {
        if (i<maxDC)
        {
            uint8_t b = (i * NUM_BRIGHTNESS / maxDC) + 1;
            sCycleTable[i] = b;
        }
        else
        {
            sCycleTable[i] = 255;  // never reach this brightness -> always off
        }
        Serial.print(i);
        Serial.print(" - ");
        Serial.println(sCycleTable[i]);
    }
}
