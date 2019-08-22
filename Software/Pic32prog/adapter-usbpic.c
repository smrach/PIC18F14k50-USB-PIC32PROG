// last edited: Sunday, 5 July, 2015 (release 2)

/*
 * Interface to PIC32 ICSP port using bitbang adapter.
 * Copyright (C) 2014 Serge Vakulenko
 *
 * Additions for talking to ascii ICSP programmer Copyright (C) 2015 Robert Rozee
 *
 * This file is part of PIC32PROG project, which is distributed
 * under the terms of the GNU General Public License (GPL).
 * See the accompanying file "COPYING" for more details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <usb.h>

#include "adapter.h"
#include "hidapi.h"
#include "pic32.h"


typedef struct {
    adapter_t adapter;              /* Common part */
	const char *name;
	hid_device *hiddev;
	unsigned char pgm_mode_active;    /* Take Track of already request to enter programming mode PIC side */
	unsigned char pgm_port_setup;     /* Take Track of request made to setup io ports on PIC side */
	unsigned char wires_mode;		  /* Take Track of what kind of connection we wants to use (JTAG or ICSP) */
    unsigned use_executive;
    unsigned serial_execution_mode;
} usb_adapter_t;

static int DBG2 = 0;    // print messages at entry to main routines
/*
 * Identifiers of USB adapter.
 */
#define usbpic_VID          0x04D8
#define usbpic_PID          0x0080  /* Stefano Tests */
/* Calculate checksum.
 */
static unsigned calculate_crc (unsigned crc, unsigned char *data, unsigned nbytes){
    static const unsigned short crc_table [16] = {
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
        0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    };
    unsigned i;

    while (nbytes--) {
        i = (crc >> 12) ^ (*data >> 4);
        crc = crc_table[i & 0x0F] ^ (crc << 4);
        i = (crc >> 12) ^ (*data >> 0);
        crc = crc_table[i & 0x0F] ^ (crc << 4);
        data++;
    }
    return crc & 0xffff;
}
/* Get the hardware configured Wire setup mode
*/
unsigned char usbpic_GetWiresMode(usb_adapter_t *a){
	int res;
	unsigned char buf [64];
	if (debug_level> 0){
		
	}
	buf[0] = 0x11;
	buf[1] = 0x00; //0 get others set.
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	res = hid_read(a->hiddev,buf, 64); 
	if (res == 0) {
		fprintf (stderr, "Timed out.\n");
		exit (-1);
	}
	if (buf[0] != 1 && buf[63] != 0x11) {
		fprintf (stderr, "uhb: error %d receiving packet\n", res);
		exit (-1);
	}
	if (debug_level> 0){
		printf("GetWiresMode(From PIC)->%02x\n", buf[1]);
	}
	return buf[1];
}
/* Set the wanted wire connection mode configuration 
*/
static void usbpic_SetWiresMode(usb_adapter_t *a, unsigned char mode){
	int res;
	unsigned char buf [64];
	
	if (a->wires_mode == mode) 
		return;
	
	if (debug_level> 0){
		printf("SetWiresMode(%02x)\n", mode);
	}
	buf[0] = 0x11;
	buf[1] = 0x01; //0 get others set.
	buf[2] = mode;
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	res = hid_read(a->hiddev,buf, 64); //hid_read_timeout (a->hiddev, a->reply, 64, 1000);
	if (res == 0) {
		fprintf (stderr, "Timed out.\n");
		exit (-1);
	}
	if (buf[0] != 1 && buf[63] != 0x11) {
		fprintf (stderr, "uhb: error %d receiving packet\n", res);
		exit (-1);
	}
	a->wires_mode = usbpic_GetWiresMode(a);
}
/* Set or Reset on PIC side the Hardware input/output ports
*/
static void usbpic_SetupIOPorts(usb_adapter_t *a, unsigned char setOrUnset){
	int res;
	unsigned char buf [64];
	if (a->pgm_port_setup == setOrUnset) //if already setup skip.
		return;
	if (debug_level> 0){
		printf("SetupIOPorts(%d)\n", setOrUnset);
	}
	buf[0] = 0x22;
	buf[1] = setOrUnset; //get others set.
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	a->pgm_port_setup = setOrUnset;
}
/* JTAG Enter PGM Mode (2-wire)
*/
static void usbpic_EnterPgm(usb_adapter_t *a){
	int res;
	unsigned char buf [64];
	
	if (debug_level> 0){
		printf("Enter PGM Mode()\n");
	}
	buf[0] = 0x83;
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	a->pgm_mode_active = 1;
}
/* JTAG Exit Pgm mode (2-wire)
*/
static void usbpic_ExitPgm(usb_adapter_t *a){
	int res;
	unsigned char buf [64];
	if (debug_level> 0){
		printf("Exit PGM Mode()\n");
	}
	buf[0] = 0x82;
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	a->pgm_mode_active = 0;
}
/* this routine performs the functions:
 * (1) power up the target, then send out the ICSP signature to enable ICSP programming mode;
 * (0) power down the target in an orderly fashion.
 * (by RR)
 */
static void set_programming_mode (usb_adapter_t *a, int ICSP_EN){
	if (ICSP_EN) {
		if(!a->pgm_mode_active)
			usbpic_EnterPgm(a);
	} else {
		if (a->pgm_mode_active)
			usbpic_ExitPgm(a);
	}
}
/*JTAG SetMode Pseudo Operation
*/
static void usbpic_SetMode(usb_adapter_t *a, unsigned char mode, unsigned char mode_bits){
	int res;
	unsigned char buf [64];
	
	buf[0] = 0x88;
	buf[1] = mode;
    buf[2] = mode_bits; 
  
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
}
/*JTAG SendCommand Pseudo Operation
*/
static void usbpic_SendCommand(usb_adapter_t  *a, unsigned char command, unsigned char cmd_bits){
	int res;
	unsigned char buf [64];
	
	buf[0] = 0x99;
	buf[1] = command;
    buf[2] = cmd_bits; //TRONCATO!
  
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
}
/* Close the adapter and remove hardware ports setup (input mode all ports)
*/
static void usbpic_close (adapter_t *adapter, int power_on){
    usb_adapter_t *a = (usb_adapter_t*) adapter;
    usbpic_SetMode(a,0x1f,6);				   // TMS 1-1-1-1-1-0 //	
     // (force the Chip TAP controller into Run Test/Idle state)
	set_programming_mode (a, 0); //Exit programming mode.
	usbpic_SetupIOPorts(a, 0); //Unset IO Ports
	free(a);
}
/* Shouldn't XferFastData check the value of PrAcc returned in
// the LSB? We don't seem to be even reading this back.
// UPDATE1: The check is not needed in 4-wire JTAG mode, seems
// to only be required if in 2-wire ICSP mode. It would also
// slow us down horribly.
// UPDATE2: We are operating at such a slow speed that the PrAcc
// check is not really needed. To date, have never seen PrAcc != 1
*/
/*static void OLDxfer_fastdata (usb_adapter_t *a, unsigned word)
{
	
    //a->FDataCount++;
	 if (DBG2)
        fprintf (stderr, "xfer_fastdata\n");
	
    if (CFG2 == 1)
        usbpic_send (a, 0, 0, 33, (unsigned long long) word << 1, 0);

    if (CFG2 == 2) {
        usbpic_send (a, 0, 0, 33, (unsigned long long) word << 1, 1);
        unsigned status = usbpic_recv (a);
        if (! (status & 1))
            printf ("!");
    }

    //
    // could add in code above to handle retrying if PrAcc == 0
    //
    // a better (faster) approach may be to 'accumulate' PrAcc at the
    // programming adaptor, and then check the value at the end of a series
    // of xfer_fastdata calls. we would need to implement an extra ascii
    // command to use instead of 'D' (0x44) in the TDI header; '+' could be
    // used as the check command (PrAcc |= TDO), while '=' used to read out
    // the result (PrAcc) and reset the accumulator (PrAcc = 1) at the
    // programming adaptor.
}
*/
/*JTAG XferData Pseudo Operation
*/
static unsigned usbpic_XferData(usb_adapter_t *a, unsigned data, unsigned char data_length) {
    unsigned result;
	int res;
	unsigned char buf [64];
	buf[0] = 0x85;
	buf[1] = data;
    buf[2] = data >> 8;
    buf[3] = data >> 16;
    buf[4] = data >> 24;
	buf[5] = data_length;
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	res = hid_read(a->hiddev,buf, 64); //hid_read_timeout (a->hiddev, a->reply, 64, 1000);
	if (res == 0) {
		fprintf (stderr, "Timed out.\n");
		exit (-1);
	}
	if (buf[0] != 1 && buf[63] != 0x85) {
		fprintf (stderr, "uhb: error %d receiving packet\n", res);
		exit (-1);
	}
	result = buf[1];
	result |= buf[2] << 8;
	result |= buf[3] << 16;
	result |= buf[4] << 24;
	
	if (result == 0xDEADBEAF) {
		fprintf (stderr, "uhb: error 0xDEADBEAF usbpic_XferInstruction \n");
		exit (-1);
	}
	return result;
}
/*JTAG XferInstruction Pseudo Operation
*/
static unsigned char usbpic_XferInstruction (usb_adapter_t *a, unsigned instruction){
	int res;
	unsigned char buf [64];
	unsigned char reply = 0;
	
	buf[0] = 0xDD;
	buf[1] = instruction;
	buf[2] = instruction >> 8;
	buf[3] = instruction >> 16;
	buf[4] = instruction >> 24;
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	res = hid_read(a->hiddev,buf, 64); 
	if (res == 0) {
		fprintf (stderr, "Timed out.\n");
		exit (-1);
	}
	if (buf[0] != 1 && buf[63] != 0xDD) {
		fprintf (stderr, "uhb: error %d receiving packet\n", res);
		exit (-1);
	}
	reply = buf[1] & 1 ? 1 : 0;
	if (debug_level > 0) {

	}
 	return reply;
}
/*static unsigned char usbpic_XferInstructionChecked (usb_adapter_t *a, unsigned instruction) {
	
	unsigned char replied = usbpic_XferInstructionD(a,instruction);
	if (!replied) {
		fprintf(stderr,"XferInstruction %08x ",instruction);
		fprintf(stderr,"-> FAILED %d\n",replied);
	}
	return replied;
}
*/
/*JTAG XferFastData Pseudo Operation
*/
static unsigned usbpic_XferFastData(usb_adapter_t *a, unsigned data){
	unsigned long result;
	int res;
	unsigned char buf [64];
	
	buf[0] = 0xA0;
	buf[1] = data;
	buf[2] = data >> 8;
	buf[3] = data >> 16;
	buf[4] = data >> 24;

	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	res = hid_read(a->hiddev,buf, 64); //hid_read_timeout (a->hiddev, a->reply, 64, 1000);
	if (res == 0) {
		fprintf (stderr, "Timed out.\n");
		exit (-1);
	}
	if (buf[0] != 1 && buf[63] != 0xA0) {
		fprintf (stderr, "uhb: error %d receiving packet\n", res);
		exit (-1);
	}
	result = buf[1];
	result |= buf[2] << 8;
	result |= buf[3] << 16;
	result |= buf[4] << 24;
	
	if (result == 0xDEADBEAF) {
		fprintf (stderr, "uhb: error 0xDEADBEAF xfer_instruction \n");
		exit (-1);
	}
	
	if (debug_level > 0) {
		/*int i;
		if (buf[5]==0){
			fprintf(stderr,"Failed to XferFastData, missed command. RCV:\n");
			for (i=0;i<8;i++){
				fprintf(stderr,"%02x ", buf[i]);
			}
			fprintf(stderr,"\n");
		} */	
	}
	return result;
}
/*static unsigned usbpic_XferFastData(usb_adapter_t *a, unsigned data) {
	unsigned reply = 0; 
	unsigned char result = 0;
	int retry = 0;
	do{
		reply = (unsigned)usbpic_XferFastDataD(a, data);
		result = (reply & 0x10000000) ? 1 : 0;
		if (!result) {
			fprintf(stderr,"(%d) XferFastData %08x -> FAILED %09x \n",retry, data, reply);
			
		}
	} while (!(result==1));
	return (unsigned)(reply&0xffffffff);
	
	return usbpic_XferFastDataD(a,data);
}
*/
/* Read the Device Identification code
 */
static unsigned usbpic_get_idcode (adapter_t *adapter){
    usb_adapter_t *a = (usb_adapter_t*) adapter;
    // Reset the JTAG TAP controller: TMS 1-1-1-1-1-0.
    // After reset, the IDCODE register is always selected.
	usbpic_SetMode(a,(unsigned char)0x1f,6);
    return usbpic_XferData(a, 0,32); // return the Read out 32 bits of data. //
}
/* Get PeResponse PIC Side
*/
static unsigned usbpic_GetPeResponse (usb_adapter_t *a){
	unsigned result;
	int res;
	unsigned char buf [64];
	
	buf[0] = 0xCC;
    if (debug_level > 1) {
		fprintf(stderr,"GetPeResponse()\n");
	}
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	res = hid_read(a->hiddev,buf, 64); //hid_read_timeout (a->hiddev, a->reply, 64, 1000);
	if (res == 0) {
		fprintf (stderr, "Timed out.\n");
		exit (-1);
	}
	if (buf[0] != 1 && buf[63] != 0xCC) {
		fprintf (stderr, "uhb: error %d receiving packet\n", res);
		exit (-1);
	}
	result = (unsigned) buf[1];
	result |= (unsigned) buf[2] << 8;
	result |= (unsigned) buf[3] << 16;
	result |= (unsigned) buf[4] << 24;
	
	if (result == 0xDEADBEAF) {
		fprintf (stderr, "uhb: error 0xDEADBEAF xfer_instruction \n");
		exit (-1);
	}
	
	if (debug_level > 1) {
		int i;
		fprintf(stderr,"PE Response->[");
		for (i=0;i<8;i++){
			fprintf(stderr,"%02x ", buf[i]);
		}
		fprintf(stderr,"]\n");
	}
	return result;
}
/* Get PE RESPONSE from Adapter side
*/
static unsigned get_pe_response (usb_adapter_t *a){
    unsigned response;
	if (debug_level > 1) {
		fprintf(stderr,"Called get_pe_response()\n");
	}
	response = usbpic_GetPeResponse(a);
	if (debug_level > 1) {
		fprintf (stderr, "got PE response %08x\n", response);
	}
	return response;
}
/* Get Multiple(s) PE Response from adapter.
*/
static void get_pe_response_multi (usb_adapter_t *a, unsigned char count, unsigned *data){
	int res, i;
	unsigned char buf [64];
	
	buf[0] = 0xC0;
	buf[1] = count;

	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	res = hid_read(a->hiddev,buf, 64); //hid_read_timeout (a->hiddev, a->reply, 64, 1000);
	if (res == 0) {
		fprintf (stderr, "Timed out.\n");
		exit (-1);
	}
	if (buf[0] != 1 && buf[63] != 0xC0) {
		fprintf (stderr, "uhb: error %d receiving packet\n", res);
		exit (-1);
	}
	if (debug_level > 0) {
/*		fprintf(stderr,"RCV:");
		for (i=0;i<16;i++){
			fprintf(stderr,"%02x ", buf[i]);
		}
		fprintf(stderr,"\n");
*/  
    }
	
	for (i=0;i<count;i++){
		*data = buf[i*4+1];
		*data |= buf[i*4+2] << 8;
		*data |= buf[i*4+3] << 16;
		*data |= buf[i*4+4] << 24;
		data++;
	}
	if (debug_level > 0) {

	}
}
/*JTAG Sequence to enter serial execution 
(if Write protected return MCHP_STATUS)
return 0 for succees or MCHP_STATUS for fail.
*/ 
static unsigned usbpic_InvokeSerialExecution(usb_adapter_t *a){
	int res;
	unsigned char buf [64];
	unsigned word;
	buf[0] = 0x86;
	buf[1] = (memcmp(a->adapter.family_name, "mx", 2)) ? 0 : 1; // 0 = mx not needed for MZ processors
	if (debug_level > 0) {
	    fprintf(stderr,"Flag MX Family ->[%02x]\n",buf[1]);	
    }
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	/* Get reply. */
	res = hid_read(a->hiddev,buf, 64); //hid_read_timeout (a->hiddev, a->reply, 64, 1000);
	if (res == 0) {
		fprintf (stderr, "Timed out.\n");
		exit (-1);
	}
	if (buf[1] != 1 && buf[63] != 0x86) { 
		fprintf (stderr, "uhb: error %d receiving packet\n", res);
		exit (-1);
	}
	word = buf[1];
	
	if (debug_level > 0) {
		
    }
	return word;
}
/* Put device in serial execution mode. This is an alternative version
 * taken directly from the microchip application note. The original
 * version threw up a status error and then did an exit(-1), but
 * when the exit was commented out still seemed to function.
 * SEE: "PIC32 Flash Programming Specification 60001145N.pdf"
 * (by RR)
 */
static void serial_execution (usb_adapter_t *a) {

    if (a->serial_execution_mode)
        return;
    a->serial_execution_mode = 1;

	    // Enter serial execution. //
    if (debug_level > 0)
        fprintf (stderr, "Enter Serial Execution\n");

	usbpic_InvokeSerialExecution(a);
}
/* Read a word from memory (without PE).
 *
 * Only used to read configuration words.
 */
static unsigned usbpic_read_word (adapter_t *adapter, unsigned addr) {
    usb_adapter_t *a = (usb_adapter_t*) adapter;
    unsigned addr_lo = addr & 0xFFFF;
    unsigned addr_hi = (addr >> 16) & 0xFFFF;
    if (DBG2)
        fprintf (stderr, "read_word\n");

    serial_execution (a);
		
	usbpic_SendCommand(a, (unsigned char)TAP_SW_ETAP, 5);
	usbpic_SetMode(a, 0x1f, 6);					      //reset etap
	usbpic_XferInstruction (a, 0x3c13ff20); 	      //lui $s3, 0xFF20 STEFANO
    usbpic_XferInstruction (a, 0x3c080000 | addr_hi); // lui t0, addr_hi
    usbpic_XferInstruction (a, 0x35080000 | addr_lo); // ori t0, addr_lo
    usbpic_XferInstruction (a, 0x8d090000);           // lw t1, 0(t0)
    usbpic_XferInstruction (a, 0xae690000);           // sw t1, 0(s3)
	usbpic_XferInstruction (a, 0);					  // nop
	
    usbpic_SendCommand(a, (unsigned char)ETAP_FASTDATA, 5);
	unsigned word = usbpic_XferFastData(a,0x00);       // Get fastdata. / 
	
	if (debug_level > 0)
        fprintf (stderr, "read word at %08x -> %08x\n", addr, word);
	return word;
}
/* Read a memory block.
 */
static void usbpic_read_data (adapter_t *adapter, unsigned addr, unsigned nwords, unsigned *data) {
	
    usb_adapter_t *a = (usb_adapter_t*) adapter;
    unsigned words_read, i;

    if (DBG2)
        fprintf (stderr, "read_data\n");

    if (! a->use_executive) {
        // Without PE. //
        for (i = nwords; i > 0; i--) {
            *data++ = usbpic_read_word (adapter, addr);
            addr += 4;
        }
        return;
    }

    // Use PE to read memory. //
    for (words_read = 0; words_read < nwords; words_read += 32) {
		usbpic_SendCommand(a,(unsigned char)ETAP_FASTDATA,5);
		usbpic_XferFastData(a, PE_READ << 16 | 32); 	// Read 32 words //
		usbpic_XferFastData(a, addr);					// Address //

        unsigned response = get_pe_response (a);        // Get response //
        if (response != PE_READ << 16) {
            fprintf (stderr, "bad pe_read_data READ response = %08x, expected %08x\n",
                                                response,     PE_READ << 16);
            exit (-1);
        }
		//Get a fast 8 response at once.
        for (i = 0; i < 32/8; i++) {
			//response = get_pe_response (a);          	// Get data //
            //if (debug_level > 0 && response > 0){
			//	fprintf(stderr,"[%08x] %08x\n",addr+i*4,response);
			//}
			//data++ = response; //get_pe_response (a);          // Get data //
			get_pe_response_multi(a, 8, data);
			data+=8;
        }
        addr += 32 * 4;
    }
}
/* Download programming executive (PE).
 */
static void usbpic_load_executive (adapter_t *adapter, const unsigned *pe, unsigned nwords, unsigned pe_version) {
	int i;
    usb_adapter_t *a = (usb_adapter_t*) adapter;

    a->use_executive = 1;
	
    serial_execution (a);
    printf ("   Loading PE: ");

    if (memcmp(a->adapter.family_name, "mz", 2) != 0) {            // steps 1. to 3. not needed for MZ processors
        // Step 1. 
        usbpic_XferInstruction (a, 0x3c04bf88);   // lui a0, 0xbf88
        usbpic_XferInstruction (a, 0x34842000);   // ori a0, 0x2000 - address of BMXCON
        usbpic_XferInstruction (a, 0x3c05001f);   // lui a1, 0x1f
        usbpic_XferInstruction (a, 0x34a50040);   // ori a1, 0x40   - a1 has 001f0040
        usbpic_XferInstruction (a, 0xac850000);   // sw  a1, 0(a0)  - BMXCON initialized
        printf ("1");

        // Step 2. 
        usbpic_XferInstruction (a, 0x34050800);   // li  a1, 0x800  - a1 has 00000800
        usbpic_XferInstruction (a, 0xac850010);   // sw  a1, 16(a0) - BMXDKPBA initialized
        printf (" 2");

        // Step 3. 
        usbpic_XferInstruction (a, 0x8c850040);   // lw  a1, 64(a0) - load BMXDMSZ
        usbpic_XferInstruction (a, 0xac850020);   // sw  a1, 32(a0) - BMXDUDBA initialized
        usbpic_XferInstruction (a, 0xac850030);   // sw  a1, 48(a0) - BMXDUPBA initialized
        printf (" 3");
    }

    // Step 4. 
    usbpic_XferInstruction (a, 0x3c04a000);   // lui a0, 0xa000
    usbpic_XferInstruction (a, 0x34840800);   // ori a0, 0x800  - a0 has a0000800
    printf (" 4 (LDR)");

    // Download the PE loader. 

    for (i = 0; i < PIC32_PE_LOADER_LEN; i += 2) {
        // Step 5. 
        unsigned opcode1 = 0x3c060000 | pic32_pe_loader[i];
        unsigned opcode2 = 0x34c60000 | pic32_pe_loader[i+1];

        usbpic_XferInstruction (a, opcode1);      // lui a2, PE_loader_hi++
        usbpic_XferInstruction (a, opcode2);      // ori a2, PE_loader_lo++
        usbpic_XferInstruction (a, 0xac860000);   // sw  a2, 0(a0)
        usbpic_XferInstruction (a, 0x24840004);   // addiu a0, 4
    }
    printf (" 5");

    // Jump to PE loader (step 6). 
    usbpic_XferInstruction (a, 0x3c19a000);   // lui t9, 0xa000
    usbpic_XferInstruction (a, 0x37390800);   // ori t9, 0x800  - t9 has a0000800
    usbpic_XferInstruction (a, 0x03200008);   // jr  t9
    usbpic_XferInstruction (a, 0x00000000);   // nop
    printf (" 6");

    // Send parameters for the loader (step 7-A).
    // PE_ADDRESS = 0xA000_0900,
    // PE_SIZE //
    usbpic_SendCommand(a,(unsigned char)ETAP_FASTDATA,5);
	usbpic_XferFastData(a,0xa0000900);
	usbpic_XferFastData(a,nwords);
    printf (" 7a (PE)");

    // Download the PE itself (step 7-B). //
    for (i = 0; i < nwords; i++) {
		usbpic_XferFastData(a, *pe++);
	}
    printf (" 7b");

    // Download the PE instructions. 
	usbpic_XferFastData(a,0);						// Step 8 - jump to PE. //
	usbpic_XferFastData(a, 0xDEAD0000);
    printf (" 8 ");

	mdelay(100);
	
	usbpic_XferFastData(a, PE_EXEC_VERSION << 16);
	unsigned version = get_pe_response(a);
    if (version != (PE_EXEC_VERSION << 16 | pe_version)) {
        fprintf (stderr, "\nbad PE version = %08x, expected %08x\n",
                       version, PE_EXEC_VERSION << 16 | pe_version);
        exit (-1);
    }
    printf ("PE version = v%04x\n", version & 0xFFFF);
}
/* Erase all flash memory.
 */
static void usbpic_erase_chip (adapter_t *adapter) {
	
    usb_adapter_t *a = (usb_adapter_t*) adapter;

	usbpic_SendCommand(a, TAP_SW_MTAP, 5);		// Send command. [
	usbpic_SendCommand(a, MTAP_COMMAND, 5);		// Send command. [
	usbpic_XferData(a, MCHP_ERASE, 8);		//XferData

    if (memcmp(a->adapter.family_name, "mz", 2) == 0)
		usbpic_XferData(a, MCHP_DEASSERT_RST, 8); // needed for PIC32MZ devices only.

    int i = 0;
    unsigned status;
    do {
        mdelay(10);
        status = usbpic_XferData(a, MCHP_STATUS, 8);
        i++;
    } while ((status & (MCHP_STATUS_CFGRDY|MCHP_STATUS_FCBUSY)) != MCHP_STATUS_CFGRDY && i < 100);

    if (i == 100) {
        fprintf (stderr, "invalid status = %04x (in erase chip)\n", status);
        exit (-1);
    }
    printf ("(%imS) ", i * 10);
}

/* Write a word to flash memory. (only seems to be used to write the four configuration words)
 *
 * !!!!!!!!!! WARNING !!!!!!!!!!
 * on PIC32MZ EC family devices PE_WORD_PROGRAM will not generate the ECC parity bits;
 * instead should use QUAD_WORD_PGRM to program all four configuration words at once.
 * !!!!!!!!!! WARNING !!!!!!!!!!
 */
static void usbpic_program_word (adapter_t *adapter, unsigned addr, unsigned word) {
	
    usb_adapter_t *a = (usb_adapter_t*) adapter;

    if (debug_level > 0)
        fprintf (stderr, "program word at %08x: %08x\n", addr, word);
    if (! a->use_executive) {
        // Without PE. 
        fprintf (stderr, "slow flash write not implemented yet\n");
        exit (-1);
    }

    if (memcmp(a->adapter.family_name, "mz", 2) == 0)
        printf("!ECC!");                        // warn if word-write to MZ processor

    // Use PE to write flash memory. 
    //usbpic_send (a, 1, 1, 5, ETAP_FASTDATA, 0);  // Send command. 
	usbpic_SendCommand(a,ETAP_FASTDATA,5); //ETAP_FASTDATA
    usbpic_XferFastData (a, PE_WORD_PROGRAM << 16 | 2);
    usbpic_XferFastData (a, addr);                    // Send address. 
    usbpic_XferFastData (a, word);                    // Send word. 

    unsigned response = get_pe_response (a);
    if (response != (PE_WORD_PROGRAM << 16)) {
        fprintf (stderr, "\nfailed to program word %08x at %08x, reply = %08x\n",
                                                   word,   addr,       response);
        exit (-1);
    }
}

/* Flash write row of memory.
 */
static void usbpic_program_row (adapter_t *adapter, unsigned addr, unsigned *data, unsigned words_per_row)
{
	
    usb_adapter_t *a = (usb_adapter_t*) adapter;

    if (debug_level > 0)
        fprintf (stderr, "row program %u words at %08x\n", words_per_row, addr);
    if (! a->use_executive) {
        // Without PE. 
        fprintf (stderr, "slow flash write not implemented yet\n");
        exit (-1);
    }

    // Use PE to write flash memory. 
    //usbpic_send (a, 1, 1, 5, ETAP_FASTDATA, 0);  // Send command. 
	usbpic_SendCommand(a,ETAP_FASTDATA,5); //ETAP_FASTDATA
    usbpic_XferFastData(a, PE_ROW_PROGRAM << 16 | words_per_row);
    usbpic_XferFastData(a, addr);                      // Send address. 

    // Download data. 
    int i;
    for (i = 0; i < words_per_row; i++) {
        usbpic_XferFastData(a, *data++);               // Send word. 
    }

    unsigned response = get_pe_response (a);
    if (response != (PE_ROW_PROGRAM << 16)) {
        fprintf (stderr, "\nfailed to program row at %08x, reply = %08x\n",
                                                     addr,         response);
        exit (-1);
    }
}

/* Verify a block of memory.
 */
static void usbpic_verify_data (adapter_t *adapter, unsigned addr, unsigned nwords, unsigned *data)
{
	usb_adapter_t *a = (usb_adapter_t*) adapter;
    unsigned data_crc, flash_crc;

    if (! a->use_executive) {
        // Without PE. 
        fprintf (stderr, "slow verify not implemented yet\n");
        exit (-1);
    }
	// Use PE to get CRC of flash memory. 
	usbpic_SendCommand(a,(unsigned char)ETAP_FASTDATA,5);
	usbpic_XferFastData (a, PE_GET_CRC << 16);
	mdelay(100);
	usbpic_XferFastData (a, addr);            // Send address. 
    mdelay(100);
	usbpic_XferFastData (a, nwords * 4);      // Send length. 
    mdelay(100);
    unsigned response = get_pe_response (a);
    if (response != (PE_GET_CRC << 16)) {
        fprintf (stderr, "\nfailed to verify %d words at %08x, reply = %08x\n",
                                             nwords,     addr,       response);
        exit (-1);
    }
    flash_crc = get_pe_response (a) & 0xffff;
    data_crc = calculate_crc (0xffff, (unsigned char*) data, nwords * 4);
    if (flash_crc != data_crc) {
        fprintf (stderr, "\nchecksum failed at %08x: returned %04x, expected %04x\n",
                                               addr,        flash_crc,     data_crc);
        exit (-1);
    }
}

/* Initialize bitbang adapter.
 * Return a pointer to a data structure, allocated dynamically.
 * When adapter not found, return 0.
 */
adapter_t *adapter_open_usbpic(const char wires_mode){ 

    usb_adapter_t *a;
	hid_device *hiddev;
	
    hiddev = hid_open (usbpic_VID, usbpic_PID, 0);
    if (! hiddev) {
        fprintf (stderr, "PIC18F USB adapter not found\n");
        return 0;
    }
    a = calloc (1, sizeof (*a));
    if (! a) {
        fprintf (stderr, "Out of memory\n");
        return 0;
    }
    a->hiddev = hiddev;
	a->name = "MRACH USB PIC32PGM v0.20";
    printf ("   MRACH USB PIC32PGM v0.20 --> Connesso [%s] wires\n\n", wires_mode == 2 ? "JTAG":"ICSP");

    a->use_executive = 0;
    a->serial_execution_mode = 0;

	usbpic_SetWiresMode(a, wires_mode);
	usbpic_SetupIOPorts(a, 1); //Init ports io.
	
/*	
    // Reset the JTAG TAP controller: TMS 1-1-1-1-1-0.
    // After reset, the IDCODE register is always selected.
    // Read out 32 bits of data. //
*/
	set_programming_mode (a, 1);
	usbpic_SetMode(a, (unsigned char)0x1f, 6);
	unsigned idcode = usbpic_XferData(a, 0, 32); // 1. (pg 20)
    if ((idcode & 0xfff) != 0x053) {
        // Microchip vendor ID is expected. //
        if (debug_level > 0 || (idcode != 0 && idcode != 0xffffffff))
            fprintf (stderr, "incompatible CPU detected, IDCODE=%08x\n", idcode);
		//usbpic_close(a, 0);
		set_programming_mode (a, 0);
		//usbpic_SetupIOPorts(a, 0); //Unset IO Ports
		free (a);
        return 0;
    }

    // Check status. //
	usbpic_SendCommand(a, (unsigned char)TAP_SW_MTAP,5);
	usbpic_SetMode(a,(unsigned char)0x1f, 6);
	usbpic_SendCommand(a, (unsigned char)MTAP_COMMAND,5);
    unsigned status = usbpic_XferData(a,MCHP_STATUS,8);
	if (debug_level > 0)
        fprintf (stderr, "MCHP Status %04x\n", status);

    if ((status & (MCHP_STATUS_CFGRDY | MCHP_STATUS_FCBUSY)) != MCHP_STATUS_CFGRDY) {
        fprintf (stderr, "invalid status = %04x (in open)\n", status);       // 5.
        //usbpic_close(a,0);
		set_programming_mode (a, 0);
		//usbpic_SetupIOPorts(a, 0); //Unset IO Ports
		free (a);
        return 0;
    }
    a->adapter.flags = AD_PROBE | AD_ERASE | AD_READ | AD_WRITE;
	
    /* User functions. */
    a->adapter.close = usbpic_close;
    a->adapter.get_idcode = usbpic_get_idcode;
    a->adapter.load_executive = usbpic_load_executive;
    a->adapter.read_word = usbpic_read_word;
    a->adapter.read_data = usbpic_read_data;
    a->adapter.verify_data = usbpic_verify_data;
    a->adapter.erase_chip = usbpic_erase_chip;
    a->adapter.program_word = usbpic_program_word;
    a->adapter.program_row = usbpic_program_row;
    return &a->adapter;
}
