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

/** Main program entry point. This routine contains the overall program flow, including initial
 *  setup of all components and the main program loop.
 */
int main(void)
{
    SetupHardware();

    LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
    sei();

    for (;;)
    {
        while (Serial_IsCharReceived()) {
            uint8_t cmd, data1, data2;

            /* TODO: make sure the first byte is a status byte (bit 7 set) and
             * discard bytes until we see one. also messages with a number of
             * bytes other than 3 needs to be handled. */
            cmd = Serial_RxByte();
            data1 = Serial_RxByte();
            data2 = Serial_RxByte();

            MIDI_EventPacket_t MIDIEvent = (MIDI_EventPacket_t)
            {
                .CableNumber = 0,
                .Command     = (cmd >> 4),

                .Data1       = cmd,
                .Data2       = data1,
                .Data3       = data2,
            };

            MIDI_Device_SendEventPacket(&Keyboard_MIDI_Interface, &MIDIEvent);
            MIDI_Device_Flush(&Keyboard_MIDI_Interface);
        }

        MIDI_EventPacket_t ReceivedMIDIEvent;
        while (MIDI_Device_ReceiveEventPacket(&Keyboard_MIDI_Interface, &ReceivedMIDIEvent))
        {
            if ((ReceivedMIDIEvent.Command == (MIDI_COMMAND_NOTE_ON >> 4)) && (ReceivedMIDIEvent.Data3 > 0))
              LEDs_SetAllLEDs(ReceivedMIDIEvent.Data2 > 64 ? LEDS_LED1 : LEDS_LED2);
            else
              LEDs_SetAllLEDs(LEDS_NO_LEDS);
        }

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

