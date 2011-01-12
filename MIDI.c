/*
             LUFA Library
     Copyright (C) Dean Camera, 2010.

  dean [at] fourwalledcubicle [dot] com
           www.lufa-lib.org
*/

/*
  Copyright 2010  Dean Camera (dean [at] fourwalledcubicle [dot] com)

  Permission to use, copy, modify, distribute, and sell this
  software and its documentation for any purpose is hereby granted
  without fee, provided that the above copyright notice appear in
  all copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting
  documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the
  software without specific, written prior permission.

  The author disclaim all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
*/

/** \file
 *
 *  Main source file for the MIDI demo. This file contains the main tasks of
 *  the demo and is responsible for the initial application hardware configuration.
 */

#include "MIDI.h"

#include <LUFA/Drivers/Peripheral/Serial.c>

typedef enum {
    STATE_UNKNOWN,
    STATE_1PARAM,
    STATE_2PARAM_1,
    STATE_2PARAM_2,
    STATE_SYSEX_0,
    STATE_SYSEX_1,
    STATE_SYSEX_2,
} midi_state;

#define MIDI_CABLE 0

/** LUFA MIDI Class driver interface configuration and state information. This structure is
 *  passed to all MIDI Class driver functions, so that multiple instances of the same class
 *  within a device can be differentiated from one another.
 */
USB_ClassInfo_MIDI_Device_t Keyboard_MIDI_Interface =
    {
        .Config =
            {
                .StreamingInterfaceNumber = 1,

                .DataINEndpointNumber      = MIDI_STREAM_IN_EPNUM,
                .DataINEndpointSize        = MIDI_STREAM_EPSIZE,
                .DataINEndpointDoubleBank  = false,

                .DataOUTEndpointNumber     = MIDI_STREAM_OUT_EPNUM,
                .DataOUTEndpointSize       = MIDI_STREAM_EPSIZE,
                .DataOUTEndpointDoubleBank = false,
            },
    };

/* Send n MIDI bytes in buf over USB */
void
midi_send (uint8_t p0, uint8_t p1, uint8_t p2, uint8_t p3)
{
    MIDI_EventPacket_t MIDIEvent = {
        .CableNumber = (p0 >> 4),
        .Command     = (p0 & 0x0f),
        .Data1       = p1,
        .Data2       = p2,
        .Data3       = p3,
    };

    MIDI_Device_SendEventPacket(&Keyboard_MIDI_Interface, &MIDIEvent);
    MIDI_Device_Flush(&Keyboard_MIDI_Interface);
}

uint8_t data[2];
midi_state state = STATE_UNKNOWN;

/* Try to write a midi byte out to USB. If it's not a complete packet yet,
 * buffer it and send it later when it's complete. */
void
usb_write (uint8_t b)
{
    uint8_t p0 = MIDI_CABLE;

    if (b >= 0xf8) {
        midi_send(p0 | 0x0f, b, 0, 0);
    } else if (b >= 0xf0) {
        switch (b) {
        case 0xf0:
            data[0] = b;
            state = STATE_SYSEX_1;
            break;
        case 0xf1:
        case 0xf3:
            data[0] = b;
            state = STATE_1PARAM;
            break;
        case 0xf2:
            data[0] = b;
            state = STATE_2PARAM_1;
            break;
        case 0xf4:
        case 0xf5:
            state = STATE_UNKNOWN;
            break;
        case 0xf6:
            midi_send(p0 | 0x05, 0xf6, 0, 0);
            state = STATE_UNKNOWN;
            break;
        case 0xf7:
            switch (state) {
            case STATE_SYSEX_0:
                midi_send(p0 | 0x05, 0xf7, 0, 0);
                break;
            case STATE_SYSEX_1:
                midi_send(p0 | 0x06, data[0], 0xf7, 0);
                break;
            case STATE_SYSEX_2:
                midi_send(p0 | 0x07, data[0], data[1], 0xf7);
                break;
            default:
                break;
            }
            state = STATE_UNKNOWN;
            break;
        }
    } else if (b >= 0x80) {
        data[0] = b;
        if (b >= 0xc0 && b <= 0xdf)
            state = STATE_1PARAM;
        else
            state = STATE_2PARAM_1;
    } else { /* b < 0x80 */
        switch (state) {
        case STATE_1PARAM:
            if (data[0] < 0xf0) {
                p0 |= data[0] >> 4;
            } else {
                p0 |= 0x02;
                state = STATE_UNKNOWN;
            }
            midi_send(p0, data[0], b, 0);
            break;
        case STATE_2PARAM_1:
            data[1] = b;
            state = STATE_2PARAM_2;
            break;
        case STATE_2PARAM_2:
            if (data[0] < 0xf0) {
                p0 |= data[0] >> 4;
                state = STATE_2PARAM_1;
            } else {
                p0 |= 0x03;
                state = STATE_UNKNOWN;
            }
            midi_send(p0, data[0], data[1], b);
            break;
        case STATE_SYSEX_0:
            data[0] = b;
            state = STATE_SYSEX_1;
            break;
        case STATE_SYSEX_1:
            data[1] = b;
            state = STATE_SYSEX_2;
            break;
        case STATE_SYSEX_2:
            midi_send(p0 | 0x04, data[0], data[1], b);
            state = STATE_SYSEX_0;
            break;
        default:
            break;
        }
    }
}

/* Read MIDI packets from USB and send them out again via UART. */
void
usb_read (MIDI_EventPacket_t *ReceivedMIDIEvent)
{
    if (ReceivedMIDIEvent->CableNumber != MIDI_CABLE)
        return;

    /* http://www.usb.org/developers/devclass_docs/midi10.pdf p.16f */
    switch (ReceivedMIDIEvent->Command) {
    case 0x0: /* misc - reserved for future use */
    case 0x1: /* cable events - reserved for future use */
        break;
    case 0x5: /* single byte system common message or sysex ends with
               * following single byte */
    case 0xf: /* single byte for transfer w/o parsing or RT messages */
        Serial_TxByte(ReceivedMIDIEvent->Data1);
        break;
    case 0x2: /* 2 byte system common message */
    case 0x6: /* sysex ends with following two bytes */
    case 0xc: /* program change */
    case 0xd: /* channel pressure */
        Serial_TxByte(ReceivedMIDIEvent->Data1);
        Serial_TxByte(ReceivedMIDIEvent->Data2);
        break;
    case 0x3: /* 3 byte system common message */
    case 0x4: /* sysex starts or continues */
    case 0x7: /* sysex ends with following three bytes */
    case 0x8: /* note off */
    case 0x9: /* note on */
    case 0xa: /* poly keypress */
    case 0xb: /* control change */
    case 0xe: /* pitchbend change */
        Serial_TxByte(ReceivedMIDIEvent->Data1);
        Serial_TxByte(ReceivedMIDIEvent->Data2);
        Serial_TxByte(ReceivedMIDIEvent->Data3);
        break;
    }
}

/** Main program entry point. This routine contains the overall program flow, including initial
 *  setup of all components and the main program loop.
 */
int main(void)
{
    SetupHardware();

    LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
    sei();

    for (;;) {
        while (Serial_IsCharReceived())
            usb_write(Serial_RxByte());

        MIDI_EventPacket_t ReceivedMIDIEvent;
        while (MIDI_Device_ReceiveEventPacket(&Keyboard_MIDI_Interface, &ReceivedMIDIEvent))
            usb_read(&ReceivedMIDIEvent);

        MIDI_Device_USBTask(&Keyboard_MIDI_Interface);
        USB_USBTask();
    }
}

/** Configures the board hardware and chip peripherals for the demo's functionality. */
void SetupHardware(void)
{
    /* Disable watchdog if enabled by bootloader/fuses */
    MCUSR &= ~(1 << WDRF);
    wdt_disable();

    /* Disable clock division */
    //clock_prescale_set(clock_div_1);

    /* Hardware Initialization */
    LEDs_Init();
    Serial_Init(31250, false);
    USB_Init();
}

/** Event handler for the library USB Connection event. */
void EVENT_USB_Device_Connect(void)
{
    LEDs_SetAllLEDs(LEDMASK_USB_ENUMERATING);
}

/** Event handler for the library USB Disconnection event. */
void EVENT_USB_Device_Disconnect(void)
{
    LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
}

/** Event handler for the library USB Configuration Changed event. */
void EVENT_USB_Device_ConfigurationChanged(void)
{
    bool ConfigSuccess = true;

    ConfigSuccess &= MIDI_Device_ConfigureEndpoints(&Keyboard_MIDI_Interface);

    LEDs_SetAllLEDs(ConfigSuccess ? LEDMASK_USB_READY : LEDMASK_USB_ERROR);
}

/** Event handler for the library USB Control Request reception event. */
void EVENT_USB_Device_ControlRequest(void)
{
    MIDI_Device_ProcessControlRequest(&Keyboard_MIDI_Interface);
}

