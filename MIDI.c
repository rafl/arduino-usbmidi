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

/* Send n MIDI bytes in buf over USB */
void
midi_send (uint8_t *buf, unsigned int n)
{
    MIDI_EventPacket_t MIDIEvent = {
        .CableNumber = 0,
        /* FIXME: system common / sysex need different commad nyble */
        .Command     = (buf[0] >> 4),
        .Data1       = buf[0],
    };

    switch (n) {
    case 3:
        MIDIEvent.Data3 = buf[2];
    case 2:
        MIDIEvent.Data2 = buf[1];
    }

    MIDI_Device_SendEventPacket(&Keyboard_MIDI_Interface, &MIDIEvent);
    MIDI_Device_Flush(&Keyboard_MIDI_Interface);
}

/* Read one MIDI byte from UART.
 *
 * If the byte is a realtime message, send it out via USB right away and return
 * false. Returns true otherwise.
 */
bool
midi_read (uint8_t *byte)
{
    *byte = Serial_RxByte();

    if (*byte < 0xf8)
        return true;

    /* realtime message */
    midi_send(byte, 1);
    return false;
}

/* Read stuff from UART, buffering it until a full MIDI packet can be sent, and
 * send it through USB. */
void
serial_read (void)
{
    static uint8_t last_status = 0;
    static unsigned int bufcur = 0;
    static uint8_t buf[3]; /* the usb midi packets have 3 bytes of data only,
                            * even though the actual midi messages might be
                            * longer */
    uint8_t byte;

    if (!midi_read(&byte))
        /* we got a realtime message and handled it. try again if
         * there's still something to read. */
        return;

    /* when reading the first byte of a message, and that byte is not a
     * status byte, we either started receiving bytes from the middle of
     * a message, which we're going to ignore, or we're in the middle of
     * a running status. */
    if (!bufcur && !(byte & (1 << 7))) {
        if (!last_status)
            return;

        buf[0] = last_status;
        bufcur = 1;
    }

    buf[bufcur++] = byte;

    /* flush when we've reached the maximum of 3 bytes */
    if (bufcur == 3) {
        midi_send(buf, 3);
        bufcur = 0;
        return;
    }

    /* otherwise see if we can expect any more bytes for the current
     * status byte and flush if we can't */

    unsigned int expect = 0;

    /* FIXME: only for voice messages */
    switch (buf[0] >> 4) {
    case 0x8: /* note on */
    case 0x9: /* note off */
    case 0xa: /* aftertouch */
    case 0xb: /* control change */
    case 0xe: /* pitch wheel */
        expect = 3;
        break;
    case 0xc: /* program change */
    case 0xd: /* channel pressure */
        expect = 2;
        break;
    case 0xf: /* system common - realtime is already covered in midi_read */
        /* FIXME: needs to reset last_status */
        switch (buf[0]) {
        case 0xf0: /* sysex start */
            /* FIXME! */
            break;
        case 0xf7: /* sysex end */
            /* FIXME! */
            break;
        case 0xf6: /* tune request */
            expect = 1;
            break;
        case 0xf1: /* mtc quarter frame message */
        case 0xf3: /* song select */
            expect = 2;
            break;
        case 0xf2: /* song position pointer */
            expect = 3;
            break;
        }
        break;
    }

    if (bufcur != expect)
        return;

    /* we got a full packet. send it out */
    midi_send(buf, bufcur);
}

/* Read MIDI packets from USB and send them out again via UART. */
void
usb_read (MIDI_EventPacket_t *ReceivedMIDIEvent)
{
    if (ReceivedMIDIEvent->CableNumber != 0)
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
            serial_read();

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

