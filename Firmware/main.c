/************************************************************************
	main.c

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

	Il PIC18F1xK50 ha disponibili solo le porte B e C
		PORTB -> 4 pins (RB4, RB5, RB6, RB7)
		PORTC -> 8 pins (RC0, RC1, RC2, RC3, RC4, RC5, RC6, RC7)[
		
						  ---------
				Vdd		=|		   |= Vss
			CLK/IN  ->	=|		   |= D+
			CLK/OUT	<-	=|		   |= D-	
Boot Enter> MCLR/RA4	=|		   |= VUSB
(ICSP)	<--	PMCLR	RC5	=|		   |= RC0	PGD --> (ICSP)	
			LED		RC4	=|		   |= RC1	PGC <->	(ICSP) 
					RC3	=|		   |= RC2	
(JTAG)O	<--	MCLR	RC6	=|		   |= RB4		
(JTAG)O	<--	TMS		RC7	=|		   |= RB5	TCK --> O(JTAG)	
(JTAG)I	-->	TDO		RB7	=|		   |= RB6	TDI --> O(JTAG)	
						  ---------
************************************************************************/

#ifndef MAIN_C
#define MAIN_C

#define WIRES

#include "HardwareProfile.h"
#include "./USB/usb.h"
#include "./USB/usb_function_hid.h"
#if defined(ICSP_WIRES)
	#include "icsp.h"
#else
	#include "pic32prog.h"
#endif
//Even if supported 18f2550 it is NOT raccomanded,this because he CAN'T run full speed at 3.3v 
//so you need to add more parts on the circuit with these PICS.
//18f14k50 can run full speed at 3.3 and the 3.3v supply can be taken from target circuit 
//(for this dont use the usb port +supply)
#if !defined(__18F14K50) & !defined(__18F4550) & !defined(__18F2550) 
	#error "This firmware is verified only on PIC18F14k50, 18F2550 & 18F4550 PIC microcontrollers."
#endif
// Firmware global variables
// Define the globals for the USB data in the USB RAM of the PIC18F14**50
#pragma udata //GP1
USB_HANDLE USBOutHandle = 0;
USB_HANDLE USBInHandle = 0;

#if defined(__18F2550) | defined(__18F4550)
	#pragma udata USB_VARIABLES=0x500
#elif defined(__18F14K50)
	#pragma udata USB_VARS=0x280
#endif
/* Helpers union/structures. */
union {
  BYTE Buffer[64];
  struct {
    BYTE RequestedCommand;
	BYTE dummy[63];
  };
  struct { //SetMode Mode, modeNBits
	BYTE RequestedCommand;
	BYTE Mode;
	BYTE ModeNbits;
	BYTE dummy[61];
  };
  struct { //SetCommand Command, cmdNBits
	BYTE RequestedCommand;
	BYTE Command;
	BYTE CmdNbits;
	BYTE dummy[61];
  };
  struct { //Raw tms_prolog_nbits, unsigned tms_prolog, unsigned tdi_nbits, unsigned long long tdi, int read_flag
    BYTE RequestedCommand;  //63
	BYTE TmsNbits;			//62
	BYTE Tms;				//61
	BYTE TdiNbits;			//60
	WORD Tdi;				//59 58 57 56 
	BYTE ReadFlag;		    //55
	BYTE dummy[54];
  };
  struct {
	BYTE RequestedCommand; //63
	UINT32 DWordValue;
	BYTE sendLength;
  };
} USBInput;
union {
  BYTE Buffer[64];
  struct {
	  BYTE ReplyStatus;
	  BYTE SendData[62];
	  BYTE ReplyCommand;
  };
  struct {
	  BYTE ReplyStatus;
	  UINT32_VAL ResponseDWord; 
	  BYTE ExtraData;
  };
  struct {
	unsigned char ReplyStatus;
	unsigned char TmsNbits;
	unsigned char Tms;
	unsigned char TdiNbits;
	unsigned long Tdi;
	unsigned char ReadFlag;
	//unsigned char ReadData[40];
  };
} USBOutput;

#pragma udata 
// Private function prototypes
static void initialisePic(void);
void processUsbCommands(void);
void USBCBSendResume(void);
void highPriorityISRCode();
void lowPriorityISRCode();
void applicationLoop(void);

#if defined(PROGRAMMABLE_WITH_USB_HID_BOOTLOADER) || defined(PROGRAMMABLE_WITH_USB_MCHPUSB_BOOTLOADER)
	extern void _startup (void);
	#pragma code REMAPPED_RESET_VECTOR = 0x1000
	void _reset (void)
	{
	    _asm goto _startup _endasm
	}
#endif

#pragma code REMAPPED_HIGH_INTERRUPT_VECTOR = 0x1008
void Remapped_High_ISR (void)
{
     _asm goto highPriorityISRCode _endasm
}

#pragma code REMAPPED_LOW_INTERRUPT_VECTOR = 0x1018
void Remapped_Low_ISR (void)
{
     _asm goto lowPriorityISRCode _endasm
}

#if !(defined(PROGRAMMABLE_WITH_USB_HID_BOOTLOADER) || defined(PROGRAMMABLE_WITH_USB_MCHPUSB_BOOTLOADER))
#pragma code HIGH_INTERRUPT_VECTOR = 0x08
void High_ISR (void)
{
     _asm goto REMAPPED_HIGH_INTERRUPT_VECTOR_ADDRESS _endasm
}

#pragma code LOW_INTERRUPT_VECTOR = 0x18
void Low_ISR (void)
{
     _asm goto REMAPPED_LOW_INTERRUPT_VECTOR_ADDRESS _endasm
}
#endif

#pragma code //page
// High-priority ISR handling function
#pragma interrupt highPriorityISRCode
void highPriorityISRCode()
{
	// Application specific high-priority ISR code goes here
	#if defined(USB_INTERRUPT)
		USBDeviceTasks(); // Perform USB device tasks
	#endif
	if (INTCONbits.TMR0IF)    // Interrupt from Timer0?
    {
		INTCONbits.TMR0IF = 0;      // Clear interrupt status
    }
}
// Low-priority ISR handling function
#pragma interruptlow lowPriorityISRCode
void lowPriorityISRCode() {
	// Application specific low-priority ISR code goes here
}

/******************************************************************************
 Main program entry point
 *****************************************************************************/
void main(void)
{   
	// Initialise and configure the PIC ready to go
    initialisePic();

	// If we are running in interrupt mode attempt to attach the USB device
    #if defined(USB_INTERRUPT)
        USBDeviceAttach();
    #endif
	// Main processing loop
    while(1)
    {
		// If we are in polling mode the USB device tasks must be processed here
		// (otherwise the interrupt is performing this task)
		#if defined(USB_POLLING)
	        USBDeviceTasks();
        #endif
        processUsbCommands();  //Se ho ricevuto dei dati via usb eseguo e replico a host.
		applicationLoop();     //Loop principale che esegue operazioni su interrupt.
    }
}

// Initialise the PIC
static void initialisePic(void)
{
	// Default all pins to digital
#if defined(__18F14K50) 
	ANSEL = 0;
#elif defined(__18F4550) | defined(__18F2550)
    ADCON1 = 0x0F;
#endif

//FIXME :: SET PORT INPUT AS DEFAULT AND TOGGLE ONLY WHEN NEEED

	// Configure ports as inputs (1) or outputs(0)
//ALL INPUTS PORTS
	TRISA = 0b11111111;
	TRISB = 0b11111111;	
	TRISC = 0b11111111; // RC3 ==> TDO == INPUT_PIN
	
	// Clear all ports
	LATA = 0b00000000;
	LATB = 0b00000000;
	LATC = 0b00000000;
	
	// If you have a VBUS sense pin (for self-powered devices when you want to detect if the USB host is connected) you have to specify your input pin in HardwareProfile.h
    #if defined(USE_USB_BUS_SENSE_IO)
    	tris_usb_bus_sense = INPUT_PIN;
    #endif
    
    // In the case of a device which can be both self-powered and bus-powered the device must respond correctly to a GetStatus (device) request and tell the host how it is currently powered.
    // To do this you must device a pin which is high when self powered and low when bus powered and define this in HardwareProfile.h
    #if defined(USE_SELF_POWER_SENSE_IO)
    	tris_self_power = INPUT_PIN;
    #endif

   	// Initialize the variable holding the USB handle for the last transmission
    USBOutHandle = 0;
    USBInHandle = 0;
    
    // Initialise the USB device
    USBDeviceInit();

}

/******************************************************************************
 Application Main Loop
 *****************************************************************************/
void applicationLoop(void)
{
	
}
/******************************************************************************
 Process USB commands
 *****************************************************************************/
void processUsbCommands(void)
{   
	// Note: For all tests we expect to receive a 64 byte packet containing
	// the command in byte[0] and then the numbers 0-62 in bytes 1-63.
	UINT8 bufferPointer;
    UINT8 expectedData;
    UINT8 dataReceivedOk = FLAG_FALSE;
	UINT8 needReply = FLAG_FALSE;
	UINT8 index = 0;
	UINT32_VAL readValue;
	UINT8* ptrMulti; 
    // Check if we are in the configured state; otherwise just return
    if((USBDeviceState < CONFIGURED_STATE) || (USBSuspendControl == 1)) { return; }
	
	// Check if data was received from the host.
    if(!HIDRxHandleBusy(USBOutHandle))
    {   
		// Clear trasmit buffer.
		for (bufferPointer = 0; bufferPointer < 64; bufferPointer++)
		{
			USBOutput.Buffer[bufferPointer] = 0;
		}
		readValue.Val = 0;
		// Command mode 
		switch(USBInput.RequestedCommand)
		{
			case 0x10: { // Get adapter info.
				expectedData = 0;
				dataReceivedOk = FLAG_TRUE;
				for (bufferPointer = 1; bufferPointer < 64; bufferPointer++)
				{
					if (USBInput.Buffer[bufferPointer] != expectedData)
						dataReceivedOk = FLAG_FALSE;
					expectedData++;
				}
				// If we got the data correctly, send the response packet
				if (dataReceivedOk == FLAG_TRUE)
				{
					expectedData = 0;
					for (bufferPointer = 0; bufferPointer < 64; bufferPointer++)
					{
						USBOutput.Buffer[bufferPointer] = expectedData;
						expectedData++;
					}
				}
				needReply = FLAG_TRUE;
				break;
			}
			case 0x11: { // Get/Set Wires mode
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_TRUE;
				if (USBInput.Buffer[1]) { //0 = get 
					SetWiresMode(USBInput.Buffer[2]);	
				}
				USBOutput.Buffer[1] = GetWiresMode();
				break;
			}
			case 0x22: { // Setup the I/O ports based on WiresMode.
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_FALSE;
				SetupIOPorts(USBInput.Buffer[1]); //Set/Unset
				break;
			}
			case 0x20: { //SET LED(S) STATUS 
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_FALSE;
				break;
			}
			case 0x78: { //JTAG Read DeviceID 
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_TRUE;
				GetDeviceId((UINT8*)&USBOutput.ResponseDWord);
				break;
			}
			case 0x7A: { //MCHP_STATUS 		  
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_TRUE;
				GetMCHPStatus((UINT8*)&USBOutput.ResponseDWord);
				break;
			}
			case 0x81: { //ReadFromAddress
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_TRUE;
				readValue = ReadFromAddress(USBInput.DWordValue);
				USBOutput.ResponseDWord = readValue;
				break;
			}
			case 0x82: { //ICSP_Exit          
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_FALSE;
				ExitPgmMode();
				break;
			}
			case 0x83: { //ICSP32_Enter       
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_FALSE;
				EnterPgmMode();
				break;
			}
			case 0x85: { //Transfer Data      
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_TRUE;
				XferData((unsigned char*)&USBInput.DWordValue, USBInput.sendLength,(unsigned char*) &USBOutput.SendData);
				break;
			}
			case 0x86: { //Enter Serial Execution Mode
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_TRUE;
				//added MX family flag.
				USBOutput.Buffer[1] = SerialExecutionMode(USBInput.Buffer[1]);
				break;
			}
			case 0x87: { //Wait ETap Ready       
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_TRUE;
				USBOutput.Buffer[1] = WaitETAP_Ready(USBInput.Command);
				break;
			}
			case 0x88: { // SetMode (mode, ModeNbits) 
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_FALSE;
				SetMode(USBInput.Mode,USBInput.ModeNbits);
				break;
			}
			case 0x99: { // SendCommand (cmd, CmdNbits) 
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_FALSE;
				SendCommand(USBInput.Command,USBInput.CmdNbits);
				break;
			}
			case 0xDD: { //XferInstruction				
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_TRUE;
				USBOutput.Buffer[1] = XferInstruction(USBInput.DWordValue);
				break;
			}
			case 0xA0:{ //XferFastData 					
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_TRUE;	
				XferFastData((unsigned char*)&USBInput.Buffer[1], (unsigned char*) &USBOutput.SendData, (unsigned char*) &USBOutput.Buffer[5]);
				break;
			}
			case 0xCC: { //GetPEResponse				
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_TRUE;
				GetPEResponse((unsigned char*) &USBOutput.SendData);
				break;
			}
			case 0xC0: { //GetPEResponseMulti MAX 15 otherwise BOOM!
				dataReceivedOk = FLAG_TRUE;
				needReply = FLAG_TRUE;
				ptrMulti = &USBOutput.SendData[0];
				while (USBInput.Buffer[1]--){
					GetPEResponse(ptrMulti);
					ptrMulti+=4;
				}
				break;
			}
#if defined(PROGRAMMABLE_WITH_USB_HID_BOOTLOADER) || defined(PROGRAMMABLE_WITH_USB_MCHPUSB_BOOTLOADER)
			case 0xFE: { // Soft RESET
				USBSoftDetach();
				Reset();
				break;
			}
#endif
			default:   { // Unknown command received
				dataReceivedOk = 0xFE; //bit 0 cleared.
				needReply = FLAG_TRUE;
				break;
			}
		}
		// Re-arm the OUT endpoint for the next packet
	    USBOutHandle = HIDRxPacket(HID_EP,(BYTE*)&USBInput,64);
  	}
	if (needReply) { //Trasmette una replica al commando ricevuto
		//Primo byte esito dell'elaborazione del commando ricevuto.
		//Ultimo byte ?la replica del codice del commando ricevuto. 
		USBOutput.ReplyStatus = dataReceivedOk;
		USBOutput.ReplyCommand = USBInput.RequestedCommand;
		// VOGLIO sempre trasmettere la risposta all'host quindi se ?occupato aspetto.
		while (HIDTxHandleBusy(USBInHandle));
		USBInHandle = HIDTxPacket(HID_EP,(BYTE*)&USBOutput,64);
	}
}
/******************************************************************************
 Execute USB commands received
 *****************************************************************************/
// USB Callback handling routines -----------------------------------------------------------
// Call back that is invoked when a USB suspend is detected
void USBCBSuspend(void)
{
}
// This call back is invoked when a wakeup from USB suspend is detected.
void USBCBWakeFromSuspend(void)
{
}
// The USB host sends out a SOF packet to full-speed devices every 1 ms.
void USBCB_SOF_Handler(void)
{
    // No need to clear UIRbits.SOFIF to 0 here. Callback caller is already doing that.
}
// The purpose of this callback is mainly for debugging during development.
// Check UEIR to see which error causes the interrupt.
void USBCBErrorHandler(void)
{
    // No need to clear UEIR to 0 here.
    // Callback caller is already doing that.
}
// Check other requests callback
void USBCBCheckOtherReq(void)
{
    USBCheckHIDRequest();
}
// Callback function is called when a SETUP, bRequest: SET_DESCRIPTOR request arrives.
void USBCBStdSetDscHandler(void)
{
    // You must claim session ownership if supporting this request
}
//This function is called when the device becomes initialized
void USBCBInitEP(void)
{
    // Enable the HID endpoint
    USBEnableEndpoint(HID_EP,USB_IN_ENABLED|USB_OUT_ENABLED|USB_HANDSHAKE_ENABLED|USB_DISALLOW_SETUP);
    
    // Re-arm the OUT endpoint for the next packet
    USBOutHandle = HIDRxPacket(HID_EP,(BYTE*)&USBInput.Buffer,64);
}
// Send resume call-back
void USBCBSendResume(void)
{
    static WORD delay_count;
    
    // Verify that the host has armed us to perform remote wakeup.
    if(USBGetRemoteWakeupStatus() == FLAG_TRUE) 
    {
        // Verify that the USB bus is suspended (before we send remote wakeup signalling).
        if(USBIsBusSuspended() == FLAG_TRUE)
        {
            USBMaskInterrupts();
            
            // Bring the clock speed up to normal running state
            USBCBWakeFromSuspend();
            USBSuspendControl = 0; 
            USBBusIsSuspended = FLAG_FALSE;

            // Section 7.1.7.7 of the USB 2.0 specifications indicates a USB
            // device must continuously see 5ms+ of idle on the bus, before it sends
            // remote wakeup signalling.  One way to be certain that this parameter
            // gets met, is to add a 2ms+ blocking delay here (2ms plus at 
            // least 3ms from bus idle to USBIsBusSuspended() == FLAG_TRUE, yeilds
            // 5ms+ total delay since start of idle).
            delay_count = 3600U;        
            do
            {
                delay_count--;
            } while(delay_count);
            
            // Start RESUME signaling for 1-13 ms
            USBResumeControl = 1;
            delay_count = 1800U;
            do
            {
                delay_count--;
            } while(delay_count);
            USBResumeControl = 0;

            USBUnmaskInterrupts();
        }
    }
}
// USB callback function handler
BOOL USER_USB_CALLBACK_EVENT_HANDLER(USB_EVENT event, void *pdata, WORD size)
{
    switch(event)
    {
        case EVENT_TRANSFER:
            // Application callback tasks and functions go here
            break;
        case EVENT_SOF:
            USBCB_SOF_Handler();
            break;
        case EVENT_SUSPEND:
            USBCBSuspend();
            break;
        case EVENT_RESUME:
            USBCBWakeFromSuspend();
            break;
        case EVENT_CONFIGURED: 
            USBCBInitEP();
            break;
        case EVENT_SET_DESCRIPTOR:
            USBCBStdSetDscHandler();
            break;
        case EVENT_EP0_REQUEST:
            USBCBCheckOtherReq();
            break;
        case EVENT_BUS_ERROR:
            USBCBErrorHandler();
            break;
        default:
            break;
    }      
    return FLAG_TRUE; 
}
#endif
