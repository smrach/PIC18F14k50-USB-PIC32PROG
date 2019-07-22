/************************************************************************
	HardwareProfile.h

    basato su usbGenericHidCommunication reference firmware 3_0_0_0
    Copyright (C) 2011 Simon Inns

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

	Email: simon.inns@gmail.com

************************************************************************/

#ifndef HARDWAREPROFILE_H
#define HARDWAREPROFILE_H
// USB stack hardware selection options ----------------------------------------------------------------
// (This section is the set of definitions required by the MCHPFSUSB framework.)
// Uncomment the following define if you wish to use the self-power sense feature 
// and define the port, pin and tris for the power sense pin below:
// #define USE_SELF_POWER_SENSE_IO
#define tris_self_power     TRISAbits.TRISA2
#if defined(USE_SELF_POWER_SENSE_IO)
	#define self_power          PORTAbits.RA2
#else
	#define self_power          1
#endif
// Uncomment the following define if you wish to use the bus-power sense feature 
// and define the port, pin and tris for the power sense pin below:
//#define USE_USB_BUS_SENSE_IO
#define tris_usb_bus_sense  TRISAbits.TRISA1
#if defined(USE_USB_BUS_SENSE_IO)
	#define USB_BUS_SENSE       PORTAbits.RA1
#else
	#define USB_BUS_SENSE       1
#endif
// Uncomment the following line to make the output HEX of this project work with the MCHPUSB Bootloader    
//#define PROGRAMMABLE_WITH_USB_MCHPUSB_BOOTLOADER
// Uncomment the following line to make the output HEX of this project work with the HID Bootloader
#define PROGRAMMABLE_WITH_USB_HID_BOOTLOADER		
// Application specific hardware definitions ------------------------------------------------------------
// Oscillator frequency (48Mhz with a 20Mhz/12Mhz external oscillator)
#define CLOCK_FREQ 48000000
// Device Vendor Indentifier (VID) (0x04D8 is Microchip's VID)
#define USB_VID	0x04D8
// Device Product Indentifier (PID) (0x0080)
#define USB_PID	0x0080
// Manufacturer string descriptor
#define MSDLENGTH	13
#define MSD		'S','t','e','f','a','n','o',' ','M','r','a','c','h'
// Product String descriptor
#define PSDLENGTH	32
#define PSD 		'P','I','C','1','8','F','1','4','K','5','0',' ','3','.','3','v',' ','P','I','C','3','2','P','R','O','G',' ','v','0','.','2','0'
// Device serial number string descriptor
#define DSNLENGTH	7
#define DSN		'M','R','C','_','0','.','2'

// Common useful definitions
#define INPUT_PIN 	1
#define OUTPUT_PIN 	0

#define FLAG_FALSE 	0
#define FLAG_TRUE 	1

#define LED_ON() 		LED = 1
#define LED_OFF() 		LED = 0

#define LED 		    LATCbits.LATC4
#define LED_TRIS		TRISCbits.RC4

#define clock_delay() Nop();Nop()

//ICSP Wire port definitions
#define WIRES_ICSP 		1
//PGD INPUT/OUTPUT 
#define PGD_READ		PORTCbits.RC0			//INPUT_PIN
#define PGD_WRITE		LATCbits.LATC0			//OUTPUT_PIN
#define PGD_TRIS		TRISCbits.RC0		
#define PGD_INPUT() 	PGD_TRIS=INPUT_PIN
#define PGD_OUTPUT() 	PGD_TRIS=OUTPUT_PIN
//PGC Only OUTPUT
#define PGC 			LATCbits.LATC1
#define PGC_TRIS		TRISCbits.RC1
#define PGC_HIGH() 		PGC = 1; clock_delay();
#define PGC_LOW() 		PGC = 0; clock_delay();
//P-MCLR icsp mode Output only
#define PMCLR			LATCbits.LATC5
#define PMCLR_TRIS		TRISCbits.RC5

#define	Init_ICSP_IO() PGD_TRIS=OUTPUT_PIN;PMCLR_TRIS=OUTPUT_PIN;PGC_TRIS=OUTPUT_PIN
#define DeInit_ICSP_IO() PGD_TRIS=INPUT_PIN;PMCLR_TRIS=INPUT_PIN;PGC_TRIS=INPUT_PIN 
#define	Init_ICSP()  Init_ICSP_IO();MCLR=0;PGD_WRITE=0;PGC=0;

#define WIRES_JTAG		2
#define TDO				PORTBbits.RB7			//INPUT_PIN
#define TDO_TRIS		TRISBbits.RB7			
#define MCLR			LATCbits.LATC6			//OUTPUT_PIN
#define MCLR_TRIS		TRISCbits.RC6			
#define TDI				LATBbits.LATB6			//OUTPUT_PIN
#define TDI_TRIS		TRISBbits.RB6			
#define TCK				LATBbits.LATB5 			//OUTPUT_PIN
#define TCK_TRIS		TRISBbits.RB5 			
#define TMS				LATCbits.LATC7			//OUTPUT_PIN
#define TMS_TRIS		TRISCbits.RC7 

#define TCK_HIGH() TCK = 1; clock_delay()
#define TCK_LOW() TCK = 0; clock_delay()

#define	Init_JTAG_IO()  TDO_TRIS=INPUT_PIN;MCLR_TRIS=OUTPUT_PIN;TDI_TRIS=OUTPUT_PIN;TCK_TRIS=OUTPUT_PIN;TMS_TRIS=OUTPUT_PIN;
#define DeInit_JTAG_IO() MCLR_TRIS=INPUT_PIN;TDI_TRIS=INPUT_PIN;TCK_TRIS=INPUT_PIN;TMS_TRIS=INPUT_PIN
#define Init_JTAG() Init_JTAG_IO();MCLR=0;TDI=0;TCK=0;

#endif