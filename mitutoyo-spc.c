/*
  Copyright 2016 Carsten Presser

  Permission to use, copy, modify, distribute, and sell this
  software and its documentation for any purpose is hereby granted
  without fee, provided that the above copyright notice appear in
  all copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting
  documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the
  software without specific, written prior permission.

  The author disclaims all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.

  mitutoyo-spc.c
  Firmware for a Atmega32U4 to read out a mitutoyo digital caliper
  Based on VirtualSerial.c by Dean Camera

  Connections:
  PD0 -> Data-in, 4k7 Pullup
  PD1 -> Clock-in, 4k7 Pullup
  PD3 -> Request-out

  Mitutoyo-SPC Connector
  1 GND
  2 DATA
  3 CK
  4 RD
  5 GND
*/

#include "mitutoyo-spc.h"

USB_ClassInfo_CDC_Device_t VirtualSerial_CDC_Interface = {
  .Config = {
             .ControlInterfaceNumber = INTERFACE_ID_CDC_CCI,
             .DataINEndpoint = {
                                .Address = CDC_TX_EPADDR,
                                .Size = CDC_TXRX_EPSIZE,
                                .Banks = 1,
                                }
             ,
             .DataOUTEndpoint = {
                                 .Address = CDC_RX_EPADDR,
                                 .Size = CDC_TXRX_EPSIZE,
                                 .Banks = 1,
                                 }
             ,
             .NotificationEndpoint = {
                                      .Address = CDC_NOTIFICATION_EPADDR,
                                      .Size = CDC_NOTIFICATION_EPSIZE,
                                      .Banks = 1,
                                      }
             ,
             }
  ,
};

/** Standard file stream for the CDC interface when set up, so that the virtual CDC COM port can be
 *  used like any regular character stream in the C APIs.
 */
static FILE USBSerialStream;
volatile uint8_t buf[13];
volatile uint8_t clocks;
volatile uint8_t byte_pos;
volatile uint8_t bit_pos;
volatile uint8_t delays;
volatile uint8_t flag;
typedef enum {
  WAIT_FINISH,
  SEND_REQ,
  WAIT_DATA,
} state_t;

/** Main program entry point. This routine contains the overall program flow, including initial
 *  setup of all components and the main program loop.
 */
int main(void)
{
  uint8_t i = 0;
  uint16_t ms_counter = 0;
  uint16_t blink_cnt = 0;
  state_t state = WAIT_FINISH;

  SetupHardware();

  /* Create a regular character stream for the interface so that it can be used with the stdio.h functions */
  CDC_Device_CreateStream(&VirtualSerial_CDC_Interface, &USBSerialStream);
  stdin = stdout = &USBSerialStream;

  GlobalInterruptEnable();

  printf("Mitutoyo SPC decoder\n");
  printf("(c) 2015 Carsten Presser <c@rstenpresser.de>n\n");

  while (true) {
    _delay_ms(1);
    ms_counter++;
    blink_cnt++;

    // blink with 2Hz
    if (blink_cnt == 250) {
      led_on();
    }
    if (blink_cnt == 500) {
      led_off();
      blink_cnt = 0;
    }
    // state machine used to send requests and decode
    switch (state) {
    default:
    case WAIT_FINISH:
      // wait 10ms before sending the next request
      pin_req_high();
      if (ms_counter == 10) {
        state = SEND_REQ;
        ms_counter = 0;
      }
      break;

    case SEND_REQ:
      // clear variables..
      clocks = 0;
      delays = 0;
      byte_pos = 0;
      bit_pos = 0;
      for (i = 0; i < 13; i++) {
        buf[i] = 0;
      }

      // send data request
      pin_req_low();
      state = WAIT_DATA;
      break;

    case WAIT_DATA:
      if (clocks == 52) {
        // stop request
        pin_req_high();
        // decode and print result
        decode();

        // go to the first state
        state = WAIT_FINISH;
        ms_counter = 0;
      }
      if (ms_counter > 250) {
        state = WAIT_FINISH;
        ms_counter = 0;
        printf("no data: is the device powered?\n");
      }
      break;
    };

    /* Must throw away unused bytes from the host, or it will lock up while waiting for the device */
    CDC_Device_ReceiveByte(&VirtualSerial_CDC_Interface);

    CDC_Device_USBTask(&VirtualSerial_CDC_Interface);
    USB_USBTask();
  }
}

void decode(void)
{
  uint8_t i = 0;
  /*
     D0  - preamble
     D1  - preamble
     D2  - preamble
     D3  - preamble
     D4  - sign: 0 -> +, 8 -> -
     D5  - data0
     D6  - data1
     D7  - data2
     D8  - data3
     D9  - data4
     D10 - data5
     D11 - decimal point
     D12 - units
   */

  // do a sanity check
  if (buf[11] != 0x02) {
    printf("data error: sign not at position 2. data = ");
    for (i = 0; i < 13; i++) {
      printf("0x%02x ", buf[i]);
    }
    printf(", flag = %d\n", flag);
    return;
  }
  // print the sign
  if (buf[4] == 0x08) {
    printf("-");
  } else {
    printf(" ");
  }
  // print the digits and decimal point
  for (i = 5; i <= 10; i++) {
    printf("%d", buf[i]);
    if ((10 - i) == buf[11]) {
      printf(".");
    }
  }
  // print the unit of measurement
  if (buf[12] == 0) {
    printf(" mm\n");
  } else {
    printf(" in\n");
  }
}

ISR(INT1_vect)
{
  // skip sampling if the last packet was not yet processed..
  if (clocks == 52) {
    flag++;
    return;
  }
  // count clocks (bits)
  clocks++;

  // read one bit
  if (bit_is_set(PIND, PORTD0)) {
    // LSB_First Magic :)
    buf[byte_pos] += 1 << bit_pos;
  }
  bit_pos++;

  // increase write pointer every 4 bits
  if (bit_pos == 4) {
    bit_pos = 0;
    byte_pos++;
  }
  return;
}

/** Helpers to switch LED on and Off */
void led_on(void)
{
  PORTE |= (1 << PE6);          // pin 6 von port E auf high schalten
}

void led_off(void)
{
  PORTE &= ~(1 << PE6);         // pin 6 von port E auf low schalten
}

/** Helpers to toogle the request-pin */
void pin_req_high(void)
{
  PORTD |= (1 << PD3);
}

void pin_req_low(void)
{
  PORTD &= ~(1 << PD3);
}

/** Configures the board hardware and chip peripherals for the demo's functionality. */
void SetupHardware(void)
{
  /* Disable watchdog if enabled by bootloader/fuses */
  MCUSR &= ~(1 << WDRF);
  wdt_disable();

  /* Disable clock division */
  clock_prescale_set(clock_div_1);

  /* Setup USB */
  USB_Init();

  /* Setup IO Pins */
  DDRD |= (1 << PD3);           // make PD3 an output
  PORTD |= (1 << PD0);          // enable pullups on PD0 and PD1
  PORTD |= (1 << PD1);

  /* Setup Interrupt on PD1 (INT1) */
  EICRA |= (1 << ISC11);        //The falling edge of INTn generates asynchronously an interrupt request.
  EIMSK |= (1 << INT1);         // activates the interrupt

  /* Setup LED */
  DDRE |= (1 << PE6);           // make PD2 an output
}

/** Event handler for the library USB Connection event. */
void EVENT_USB_Device_Connect(void)
{
  return;
}

/** Event handler for the library USB Disconnection event. */
void EVENT_USB_Device_Disconnect(void)
{
  return;
}

/** Event handler for the library USB Configuration Changed event. */
void EVENT_USB_Device_ConfigurationChanged(void)
{
  bool ConfigSuccess = true;

  ConfigSuccess &=
      CDC_Device_ConfigureEndpoints(&VirtualSerial_CDC_Interface);

}

/** Event handler for the library USB Control Request reception event. */
void EVENT_USB_Device_ControlRequest(void)
{
  CDC_Device_ProcessControlRequest(&VirtualSerial_CDC_Interface);
}

/** CDC class driver callback function the processing of changes to the virtual
 *  control lines sent from the host..
 *
 *  \param[in] CDCInterfaceInfo  Pointer to the CDC class interface configuration structure being referenced
 */
void
EVENT_CDC_Device_ControLineStateChanged(USB_ClassInfo_CDC_Device_t *
                                        const CDCInterfaceInfo)
{
  /* You can get changes to the virtual CDC lines in this callback; a common
     use-case is to use the Data Terminal Ready (DTR) flag to enable and
     disable CDC communications in your application when set to avoid the
     application blocking while waiting for a host to become ready and read
     in the pending data from the USB endpoints.
   */
  bool HostReady =
      (CDCInterfaceInfo->State.
       ControlLineStates.HostToDevice & CDC_CONTROL_LINE_OUT_DTR) != 0;
}
