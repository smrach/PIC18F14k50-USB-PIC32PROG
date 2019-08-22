/*
 * Interface to PIC32 JTAG port using bitbang adapter.
 *
 * Copyright (C) 2014 Serge Vakulenko
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
    /* Common part */
    adapter_t adapter;
    const char *name;
    hid_device *hiddev;
	unsigned long long read_word;
    unsigned use_executive;
    unsigned serial_execution_mode;
} usbjtag_adapter_t;

/*
 * Identifiers of USB adapter.
 */
#define USBJTAG_VID          0x04D8
#define USBJTAG_PID          0x0080  /* Stefano Tests */
//Buffer[1] is reply ok to command and last byte is the replied command.
/*
 * Calculate checksum.
 */
static unsigned calculate_crc (unsigned crc, unsigned char *data, unsigned nbytes)
{
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

/* Get the DeviceId (OK)
*/
static unsigned usbjtag_GetDeviceId(usbjtag_adapter_t *a){
	int res;
	unsigned char buf [64];
	unsigned word = 0;
	int i;
	
	if (debug_level> 0){
		printf("CALL()->usbjtag_GetDeviceId.()\n");
	}
	
	buf[0] = 0x78;
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
	if (buf[0] != 1 && buf[63] != 0x78) {
		fprintf (stderr, "uhb: error %d receiving packet\n", res);
		exit (-1);
	}
	word = buf[1];
	word |= buf[2] << 8;
	word |= buf[3] << 16;
	word |= buf[4] << 24;
	
	if (word == 0xDEADBEAF) {
		fprintf (stderr, "uhb: error 0xDEADBEAF xfer_instruction \n");
		exit (-1);
	}
	if (debug_level > 0) {
		fprintf(stderr,"RCV:");
		for (i=0;i<8;i++){
			fprintf(stderr,"%02x ", buf[i]);
		}
		fprintf(stderr,"\n");
    }
	return word;
}
/* Get the Value of MCHP_STATUS !!(Toggle MCLR=0;)!!
*/
static unsigned usbjtag_GetMchpStatus(usbjtag_adapter_t *a){
	int res;
	unsigned char buf [64];
	unsigned word = 0;
	int i;
	
	if (debug_level> 0){
		printf("CALL()->usbjtag_GetMchpStatus.()\n");
	}
	
	buf[0] = 0x7A;
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
	if (buf[0] != 1 && buf[63] != 0x7A) {
		fprintf (stderr, "uhb: error %d receiving packet\n", res);
		exit (-1);
	}
	word = buf[1];
	word |= buf[2] << 8;
	word |= buf[3] << 16;
	word |= buf[4] << 24;
	
	if (debug_level > 0) {
		fprintf(stderr,"RCV:");
		for (i=0;i<16;i++){
			fprintf(stderr,"%02x ", buf[i]);
		}
		fprintf(stderr,"\n");
    }
	return word;
}
/* Helper Leds/Ports..
*/
static void usbjtag_SetLedsStatus(usbjtag_adapter_t *a, unsigned char led1, unsigned char led2, unsigned char led3, unsigned char led4){
	int res;
	unsigned char buf [64];
	if (debug_level> 0){
		printf("CALL()->usbjtag_SetLedsStatus.()\n");
	}
	buf[0] = 0x20;
	buf[1] = led1;
	buf[2] = led2;
	buf[3] = led3;
	buf[4] = led4;
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	} 
}
/*JTAG SetMode Pseudo Operation
*/
static void usbjtag_SetMode(usbjtag_adapter_t *a, unsigned char mode, unsigned char mode_bits)
{
	int res;
	unsigned char buf [64];
	
	if (debug_level> 0){
		printf("CALL()->usbjtag_SetMode(%02x,%d)\n",mode,mode_bits);
	}
	buf[0] = 0x88;
	buf[1] = mode;
    buf[2] = mode_bits; //TRONCATO!
  
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	if (debug_level > 0) {
		
    }
}
/*JTAG SendCommand Pseudo Operation
*/
static void usbjtag_SendCommand(usbjtag_adapter_t *a, unsigned char command, unsigned char cmd_bits)
{
	int res;
	unsigned char buf [64];

	if (debug_level> 0){
		printf("CALL()->usbjtag_SendCommand(%02x,%d)\n",command,cmd_bits);
	}
	buf[0] = 0x99;
	buf[1] = command;
    buf[2] = cmd_bits; //TRONCATO!
  
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	if (debug_level > 0) {
	
    }
}
/*JTAG XferInstruction Pseudo Operation
*/
static unsigned usbtag_XferInstruction(usbjtag_adapter_t *a, unsigned instruction)
{
	int res;
	unsigned result;
	unsigned char buf [64];
	buf[0] = 0xDD;
	buf[1] = instruction;
    buf[2] = instruction >> 8;
    buf[3] = instruction >> 16;
    buf[4] = instruction >> 24;
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	res = hid_read(a->hiddev,buf, 64); //hid_read_timeout (a->hiddev, a->reply, 64, 1000);
	if (res == 0) {
		fprintf (stderr, "Timed out.\n");
		exit (-1);
	}
	if (buf[0] != 0 && buf[63] != 0xDD) {
		fprintf (stderr, "uhb: error %d receiving packet\n", res);
		exit (-1);
	}
	result = buf[1];
	if (debug_level > 0) {
		if (!result){
			fprintf(stderr,"result 0 -> instruction [%08x] not sent!\n",instruction);
		}
    }
	return result;
}/*JTAG XferData Pseudo Operation
*/
/*JTAG XferData Pseudo Operation
*/
static unsigned usbtag_XferData(usbjtag_adapter_t *a, unsigned data, unsigned char data_length)
{
	unsigned result;
	int res;
	unsigned char buf [64];
	int i;
	if (debug_level> 0){
		printf("CALL()->usbtag_XferData.()\n");
	}
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
	if (buf[1] != 0 && buf[63] != 0x85) {
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
		fprintf(stderr,"RCV:");
		for (i=0;i<8;i++){
			fprintf(stderr,"%02x ", buf[i]);
		}
		fprintf(stderr,"\n");
    }
	return result;
}
/*JTAG XferData Pseudo Operation
*/
static unsigned usbtag_XferFastData(usbjtag_adapter_t *a, unsigned data)
{
	unsigned result;
	int res;
	unsigned char buf [64];
	int i;
	if (debug_level> 0){
		printf("CALL()->usbtag_XferFastData.()\n");
	}
	buf[0] = 0x84;
	buf[1] = data;
    buf[2] = data >> 8;
    buf[3] = data >> 16;
    buf[4] = data >> 24;
	buf[5] = 32;
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	res = hid_read(a->hiddev,buf, 64); //hid_read_timeout (a->hiddev, a->reply, 64, 1000);
	if (res == 0) {
		fprintf (stderr, "Timed out.\n");
		exit (-1);
	}
	if (buf[1] != 1 && buf[63] != 0x84) {
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
		fprintf(stderr,"RCV:");
		for (i=0;i<8;i++){
			fprintf(stderr,"%02x ", buf[i]);
		}
		fprintf(stderr,"\n");
    }
	return result;
}
/*JTAG Sequence to enter serial execution 
(if Write protected return MCHP_STATUS)
return 0 for succees or MCHP_STATUS for fail.
*/ 
static unsigned usbjtag_InvokeSerialExecution(usbjtag_adapter_t *a)
{
	int res;
	unsigned char buf [64];
	int i;
	unsigned word;
	if (debug_level> 0){
		printf("CALL()->usbjtag_InvokeSerialExecution.()\n");
	}
	buf[0] = 0x86;
	buf[1] = memcmp(a->adapter.family_name, "mz", 2);     // not needed for MZ processors
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
		fprintf(stderr,"RCV:");
		for (i=0;i<8;i++){
			fprintf(stderr,"%02x ", buf[i]);
		}
		fprintf(stderr,"\n");
    }
	return word;
}
/* JTAG Enter PGM Mode
*/
static void usbjtag_EnterPgm(usbjtag_adapter_t *a)
{
	int res;
	unsigned char buf [64];
	
	if (debug_level> 0){
		printf("CALL()->usbjtag_reset.()\n");
	}
	buf[0] = 0x83;
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
}
/* JTAG Reset status
!! To Rename in ExitPGMmode
*/
static void usbjtag_reset(usbjtag_adapter_t *a, int trst, int sysrst, int led)
{
	int res;
	unsigned char buf [64];
	if (debug_level> 0){
		printf("CALL()->usbjtag_reset.()\n");
	}
	buf[0] = 0x82;
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
}

static void usbjtag_send(usbjtag_adapter_t *a, unsigned char tms_prolog_nbits, unsigned char tms_prolog, unsigned char tdi_nbits, unsigned long long tdi, int read_flag)
{
	//int res;
	//unsigned char buf [64];
	//unsigned long long word = 0;
	
	if (debug_level> 0){
		printf("!!EMPTY!!()->usbjtag_send.()\n");
	}
	/*
	buf[0] = 0xAA;
	buf[1] = read_flag;
    buf[2] = tms_prolog_nbits; //TRONCATO!
    buf[3] = tms_prolog;	   //TRONCATO!	
    buf[4] = tdi_nbits; 	   //TRONCATO!	 
	buf[5] = tdi;
    buf[6] = tdi >> 8;
    buf[7] = tdi >> 16;
    buf[8] = tdi >> 24;
	buf[9] = tdi >> 32;
	buf[10] = tdi >> 40;
	buf[11] = tdi >> 48;
	buf[12] = tdi >> 54;
	res = hid_write(a->hiddev, buf, 64);
	if (res < 0) {
		printf("Unable to write()\n");
	}
	if (debug_level > 0) {
		if (tdi_nbits > 33)
		  fprintf (stderr, " %I64u", tdi);
    }
	*/
	/* Get reply. */
	/*
	if (read_flag>0) {
		res = hid_read(a->hiddev,buf, 64); //hid_read_timeout (a->hiddev, a->reply, 64, 1000);
		if (res == 0) {
			fprintf (stderr, "Timed out.\n");
			exit (-1);
		}
		if (buf[1] != 0 && buf[63] != 0xAA) {
			fprintf (stderr, "uhb: error %d receiving packet\n", res);
			exit (-1);
		}
		word = buf[1];
		word |= buf[2] << 8;
		word |= buf[3] << 16;
		word |= buf[4] << 24;
		word |= (unsigned long long)buf[5] << 32;
		word |= (unsigned long long)buf[6] << 40;
		word |= (unsigned long long)buf[7] << 48;
		word |= (unsigned long long)buf[8] << 54;
		a->read_word = word;
	}
	*/
}

static unsigned long long usbjtag_recv (usbjtag_adapter_t *a)
{
    //unsigned long long word;
    /* Last Send a packet with read_flag set the value of read_word. */
    return a->read_word;
}

static void usbjtag_speed (usbjtag_adapter_t *a, int khz)
{
    // TODO: Set the clock rate of bitbang adapter.
}

static void usbjtag_close (adapter_t *adapter, int power_on)
{
    usbjtag_adapter_t *a = (usbjtag_adapter_t*) adapter;

    /* Clear EJTAGBOOT mode. */
    usbjtag_SendCommand(a,TAP_SW_ETAP,5);//usbjtag_send (a, 1, 1, 5, TAP_SW_ETAP, 0);    /* Send command. */
	usbjtag_SetMode(a,0x1F,6); 			 //usbjtag_send (a, 6, 31, 0, 0, 0);             /* TMS 1-1-1-1-1-0 */
    /* Toggle /SYSRST. */
    usbjtag_reset (a, 0, 1, 1);
    usbjtag_reset (a, 0, 0, 0);

    free (a);
}

/*
 * Read the Device Identification code
 */
static unsigned usbjtag_get_idcode (adapter_t *adapter)
{
    usbjtag_adapter_t *a = (usbjtag_adapter_t*) adapter;
    /* Read out 32 bits of data. */
    return  usbjtag_GetDeviceId(a);
}

/*
 * Put device to serial execution mode.
 */
static void serial_execution (usbjtag_adapter_t *a)
{
    if (a->serial_execution_mode)
        return;
    a->serial_execution_mode = 1;

    /* Enter serial execution. */
    if (debug_level > 0)
        fprintf (stderr, "%s: enter serial execution\n", a->name);

	/* Check status. */
    //usbjtag_send (a, 1, 1, 5, TAP_SW_MTAP, 0);    /* Send command. */             // 1.
    //usbjtag_send (a, 1, 1, 5, MTAP_COMMAND, 0);   /* Send command. */             // 2.
    //usbjtag_send (a, 0, 0, 8, MCHP_STATUS, 1);    /* Xfer data. */                // 3.
    
	unsigned status = usbjtag_InvokeSerialExecution(a);
    if (debug_level > 0)
        fprintf (stderr, "status %04x\n", status);
    if ((status & MCHP_STATUS_CPS) == 0) {
        fprintf (stderr, "invalid status = %04x (code protection)\n", status);    // 4.
        exit (-1);
    }
	
    //usbjtag_send (a, 0, 0, 8, MCHP_ASSERT_RST, 0);  /* Xfer data. */              // 5.
    //usbjtag_send (a, 1, 1, 5, TAP_SW_ETAP, 0);    /* Send command. */             // 6.
    //usbjtag_send (a, 1, 1, 5, ETAP_EJTAGBOOT, 0); /* Send command. */             // 7.
    //usbjtag_send (a, 1, 1, 5, TAP_SW_MTAP, 0);    /* Send command. */             // 8.
    //usbjtag_send (a, 1, 1, 5, MTAP_COMMAND, 0);   /* Send command. */             // 9.
    //usbjtag_send (a, 0, 0, 8, MCHP_DEASSERT_RST, 0);  /* Xfer data. */            // 10.

    //if (memcmp(a->adapter.family_name, "mz", 2) != 0)     // not needed for MZ processors
    //    usbjtag_send (a, 0, 0, 8, MCHP_FLASH_ENABLE, 0);  /* Xfer data. */        // 11.

    //usbjtag_send (a, 1, 1, 5, TAP_SW_ETAP, 0);    /* Send command. */             // 12.

}

static void xfer_fastdata (usbjtag_adapter_t *a, unsigned word)
{
    usbjtag_send (a, 0, 0, 33, (unsigned long long) word << 1, 0);
}

static unsigned get_pe_response (usbjtag_adapter_t *a)
{
	int res;
	unsigned char buf [64];
	unsigned word = 0;
	buf[0] = 0xC0;
	buf[1] = 0x01; //num of response...
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
	if (buf[1] != 0 && buf[63] != 0xC0) {
		fprintf (stderr, "uhb: error %d receiving packet\n", res);
		exit (-1);
	}
	word = buf[1];
	word |= buf[2] << 8;
	word |= buf[3] << 16;
	word |= buf[4] << 24;
	
	if (word == 0xDEADBEAF) {
		fprintf (stderr, "uhb: error 0xDEADBEAF xfer_instruction \n");
		exit (-1);
	}
	return word;
}
static void get_pe_response_multi (usbjtag_adapter_t *a,unsigned char count,unsigned int *response)
{
	int res;
	unsigned char buf [64];
	unsigned int word = 0;
	int index = 1;
	int i = 0;
	buf[0] = 0xC0;
	buf[1] = count; //num of read data 
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
	if (buf[1] != 0 && buf[63] != 0xC0) {
		fprintf (stderr, "uhb: error %d receiving packet\n", res);
		exit (-1);
	}
	for (i=0;i<count;i++) {
		word = buf[index++];
		word |= buf[index++] << 8;
		word |= buf[index++] << 16;
		word |= buf[index++] << 24;
		response[i] = word;
	}
}
/*
 * Read a word from memory (without PE).
 */
static unsigned usbjtag_read_word (adapter_t *adapter, unsigned addr)
{
    usbjtag_adapter_t *a = (usbjtag_adapter_t*) adapter;
    unsigned addr_lo = addr & 0xFFFF;
    unsigned addr_hi = (addr >> 16) & 0xFFFF;

    serial_execution (a);

    //fprintf (stderr, "%s: read word from %08x\n", a->name, addr);
    usbtag_XferInstruction (a, 0x3c04bf80);           // lui s3, 0xFF20
    usbtag_XferInstruction (a, 0x3c080000 | addr_hi); // lui t0, addr_hi
    usbtag_XferInstruction (a, 0x35080000 | addr_lo); // ori t0, addr_lo
    usbtag_XferInstruction (a, 0x8d090000);           // lw t1, 0(t0)
    usbtag_XferInstruction (a, 0xae690000);           // sw t1, 0(s3)
    usbtag_XferInstruction (a, 0);           			// nop see -> DS60001145Q-page 18
    //usbjtag_send (a, 1, 1, 5, ETAP_FASTDATA, 0);  /* Send command. */
    //usbjtag_send (a, 0, 0, 33, 0, 1);             /* Get fastdata. */
    usbjtag_SendCommand(a, ETAP_FASTDATA,5);
	unsigned word = usbtag_XferFastData(a,0);//usbjtag_recv (a) >> 1;

    if (debug_level > 0)
        fprintf (stderr, "%s: read word at %08x -> %08x\n", a->name, addr, word);
    return word;
}

/*
 * Read a memory block.
 */
static void usbjtag_read_data (adapter_t *adapter,
    unsigned addr, unsigned nwords, unsigned *data)
{
    usbjtag_adapter_t *a = (usbjtag_adapter_t*) adapter;
    unsigned words_read, i;

    //fprintf (stderr, "%s: read %d bytes from %08x\n", a->name, nwords*4, addr);
    if (! a->use_executive) {
        /* Without PE. */
        for (; nwords > 0; nwords--) {
            *data++ = usbjtag_read_word (adapter, addr);
            addr += 4;
        }
        return;
    }

    /* Use PE to read memory. */
    for (words_read = 0; words_read < nwords; words_read += 32) {

        usbjtag_send (a, 1, 1, 5, ETAP_FASTDATA, 0);
        xfer_fastdata (a, PE_READ << 16 | 32);      /* Read 32 words */
        xfer_fastdata (a, addr);                    /* Address */

        unsigned response = get_pe_response (a);    /* Get response */
        if (response != PE_READ << 16) {
            fprintf (stderr, "%s: bad READ response = %08x, expected %08x\n",
                a->name, response, PE_READ << 16);
            exit (-1);
        }
        for (i=0; i<4; i++) {
            //*data++ = get_pe_response (a);          /* Get data */
			get_pe_response_multi(a, 8, data);
			data+=8;
        }
        addr += 32*4;
    }
}

/*
 * Download programming executive (PE).
 */
static void usbjtag_load_executive (adapter_t *adapter,
    const unsigned *pe, unsigned nwords, unsigned pe_version)
{
    usbjtag_adapter_t *a = (usbjtag_adapter_t*) adapter;

    a->use_executive = 1;
    serial_execution (a);

    printf ("   Loading PE: ");

    if (memcmp(a->adapter.family_name, "mz", 2) != 0) {            // steps 1. to 3. not needed for MZ processors
        /* Step 1. */
        usbtag_XferInstruction (a, 0x3c04bf88);   // lui a0, 0xbf88
        usbtag_XferInstruction (a, 0x34842000);   // ori a0, 0x2000 - address of BMXCON
        usbtag_XferInstruction (a, 0x3c05001f);   // lui a1, 0x1f
        usbtag_XferInstruction (a, 0x34a50040);   // ori a1, 0x40   - a1 has 001f0040
        usbtag_XferInstruction (a, 0xac850000);   // sw  a1, 0(a0)  - BMXCON initialized
        printf ("1");

        /* Step 2. */
        usbtag_XferInstruction (a, 0x34050800);   // li  a1, 0x800  - a1 has 00000800
        usbtag_XferInstruction (a, 0xac850010);   // sw  a1, 16(a0) - BMXDKPBA initialized
        printf (" 2");

        /* Step 3. */
        usbtag_XferInstruction (a, 0x8c850040);   // lw  a1, 64(a0) - load BMXDMSZ
        usbtag_XferInstruction (a, 0xac850020);   // sw  a1, 32(a0) - BMXDUDBA initialized
        usbtag_XferInstruction (a, 0xac850030);   // sw  a1, 48(a0) - BMXDUPBA initialized
        printf (" 3");
    }

    /* Step 4. */
    usbtag_XferInstruction (a, 0x3c04a000);   // lui a0, 0xa000
    usbtag_XferInstruction (a, 0x34840800);   // ori a0, 0x800  - a0 has a0000800
    printf (" 4 (LDR)");

    /* Download the PE loader. */
    int i;
    for (i = 0; i < PIC32_PE_LOADER_LEN; i += 2) {
        /* Step 5. */
        unsigned opcode1 = 0x3c060000 | pic32_pe_loader[i];
        unsigned opcode2 = 0x34c60000 | pic32_pe_loader[i+1];

        usbtag_XferInstruction(a, opcode1);      // lui a2, PE_loader_hi++
        usbtag_XferInstruction(a, opcode2);      // ori a2, PE_loader_lo++
        usbtag_XferInstruction(a, 0xac860000);   // sw  a2, 0(a0)
        usbtag_XferInstruction(a, 0x24840004);   // addiu a0, 4
    }
    printf (" 5");

    /* Jump to PE loader (step 6). */
    usbtag_XferInstruction (a, 0x3c19a000);   // lui t9, 0xa000
    usbtag_XferInstruction (a, 0x37390800);   // ori t9, 0x800  - t9 has a0000800
    usbtag_XferInstruction (a, 0x03200008);   // jr  t9
    usbtag_XferInstruction (a, 0x00000000);   // nop
    printf (" 6");

    /* Switch from serial to fast execution mode. */
    //bitbang_send (a, 1, 1, 5, TAP_SW_ETAP, 0);
    //bitbang_send (a, 6, 31, 0, 0, 0);             /* TMS 1-1-1-1-1-0 */

    //
    // the above two lines are not present in the Microchip programming specs
    //

    /* Send parameters for the loader (step 7-A).
     * PE_ADDRESS = 0xA000_0900,
     * PE_SIZE */
    usbjtag_send (a, 1, 1, 5, ETAP_FASTDATA, 0);  /* Send command. */
    xfer_fastdata (a, 0xa0000900);
    xfer_fastdata (a, nwords);
    printf (" 7a (PE)");

    /* Download the PE itself (step 7-B). */
    for (i = 0; i < nwords; i++) {
        xfer_fastdata (a, *pe++);
    }
    //mdelay(3000);
    printf (" 7b");

    /* Download the PE instructions. */
    xfer_fastdata (a, 0);                       /* Step 8 - jump to PE. */
    xfer_fastdata (a, 0xDEAD0000);
    //mdelay(3000);
    printf (" 8");

    xfer_fastdata (a, PE_EXEC_VERSION << 16);
    unsigned version = get_pe_response (a);
    if (version != (PE_EXEC_VERSION << 16 | pe_version)) {
        fprintf (stderr, "\nbad PE version = %08x, expected %08x\n",
                       version, PE_EXEC_VERSION << 16 | pe_version);
        exit (-1);
    }

    printf (" v%04x\n", version & 0xFFFF);

    if (debug_level > 0)
        fprintf (stderr, "%s: PE version = %04x\n",
            a->name, version & 0xffff);
}

/*
 * Erase all flash memory.
 */
static void usbjtag_erase_chip (adapter_t *adapter)
{
    usbjtag_adapter_t *a = (usbjtag_adapter_t*) adapter;

    usbjtag_send (a, 1, 1, 5, TAP_SW_MTAP, 0);    /* Send command. */
    usbjtag_send (a, 1, 1, 5, MTAP_COMMAND, 0);   /* Send command. */
    usbjtag_send (a, 0, 0, 8, MCHP_ERASE, 0);     /* Xfer data. */
    //mdelay (400);

    if (memcmp(a->adapter.family_name, "mz", 2) == 0)
        usbjtag_send (a, 0, 0, 8, MCHP_DEASSERT_RST, 0);      // needed for PIC32MZ devices only.

    int i = 0;
    unsigned status;
    do {
        mdelay (100);
        usbjtag_send (a, 0, 0, 8, MCHP_STATUS, 1);    /* Xfer data. */
        status = usbjtag_recv (a);
        i++;
    } while ((status & (MCHP_STATUS_CFGRDY |
                        MCHP_STATUS_FCBUSY)) != MCHP_STATUS_CFGRDY && i < 100);

    if (i == 100) {
        fprintf (stderr, "invalid status = %04x (in erase chip)\n", status);
        exit (-1);
    }
    printf ("(%imS) ", i * 10);
}

/*
 * Write a word to flash memory.
 */
static void usbjtag_program_word (adapter_t *adapter,
    unsigned addr, unsigned word)
{
    usbjtag_adapter_t *a = (usbjtag_adapter_t*) adapter;

    if (debug_level > 0)
        fprintf (stderr, "%s: program word at %08x: %08x\n", a->name, addr, word);
    if (! a->use_executive) {
        /* Without PE. */
        fprintf (stderr, "%s: slow flash write not implemented yet.\n", a->name);
        exit (-1);
    }

    /* Use PE to write flash memory. */
    usbjtag_send (a, 1, 1, 5, ETAP_FASTDATA, 0);  /* Send command. */
    xfer_fastdata (a, PE_WORD_PROGRAM << 16 | 2);
    xfer_fastdata (a, addr);                    /* Send address. */
    xfer_fastdata (a, word);                    /* Send word. */

    unsigned response = get_pe_response (a);
    if (response != (PE_WORD_PROGRAM << 16)) {
        fprintf (stderr, "%s: failed to program word %08x at %08x, reply = %08x\n",
            a->name, word, addr, response);
        exit (-1);
    }
}

/*
 * Flash write row of memory.
 */
static void usbjtag_program_row (adapter_t *adapter, unsigned addr,
    unsigned *data, unsigned words_per_row)
{
    usbjtag_adapter_t *a = (usbjtag_adapter_t*) adapter;
    int i;

    if (debug_level > 0)
        fprintf (stderr, "%s: row program %u words at %08x\n",
            a->name, words_per_row, addr);
    if (! a->use_executive) {
        /* Without PE. */
        fprintf (stderr, "%s: slow flash write not implemented yet.\n", a->name);
        exit (-1);
    }

    /* Use PE to write flash memory. */
    usbjtag_send (a, 1, 1, 5, ETAP_FASTDATA, 0);  /* Send command. */
    xfer_fastdata (a, PE_ROW_PROGRAM << 16 | words_per_row);
    xfer_fastdata (a, addr);                    /* Send address. */

    /* Download data. */
    for (i = 0; i < words_per_row; i++) {
        xfer_fastdata (a, *data++);             /* Send word. */
    }

    unsigned response = get_pe_response (a);
    if (response != (PE_ROW_PROGRAM << 16)) {
        fprintf (stderr, "%s: failed to program row at %08x, reply = %08x\n",
            a->name, addr, response);
        exit (-1);
    }
}

/*
 * Verify a block of memory.
 */
static void usbjtag_verify_data (adapter_t *adapter,
    unsigned addr, unsigned nwords, unsigned *data)
{
    usbjtag_adapter_t *a = (usbjtag_adapter_t*) adapter;
    unsigned data_crc, flash_crc;

    //fprintf (stderr, "%s: verify %d words at %08x\n", a->name, nwords, addr);
    if (! a->use_executive) {
        /* Without PE. */
        fprintf (stderr, "%s: slow verify not implemented yet.\n", a->name);
        exit (-1);
    }

    /* Use PE to get CRC of flash memory. */
    usbjtag_send (a, 1, 1, 5, ETAP_FASTDATA, 0);  /* Send command. */
    xfer_fastdata (a, PE_GET_CRC << 16);
    xfer_fastdata (a, addr);                    /* Send address. */
    xfer_fastdata (a, nwords * 4);              /* Send length. */
    unsigned response = get_pe_response (a);
    if (response != (PE_GET_CRC << 16)) {
        fprintf (stderr, "%s: failed to verify %d words at %08x, reply = %08x\n",
            a->name, nwords, addr, response);
        exit (-1);
    }
    flash_crc = get_pe_response (a) & 0xffff;
    data_crc = calculate_crc (0xffff, (unsigned char*) data, nwords * 4);
    if (flash_crc != data_crc) {
        fprintf (stderr, "%s: checksum failed at %08x: sum=%04x, expected=%04x\n",
            a->name, addr, flash_crc, data_crc);
        //exit (-1);
    }
}

/*
 * Initialize adapter F2232.
 * Return a pointer to a data structure, allocated dynamically.
 * When adapter not found, return 0.
 */
adapter_t *adapter_open_bitbang () //const char *port, int baud_rate)
{
    usbjtag_adapter_t *a;
    hid_device *hiddev;

    hiddev = hid_open (USBJTAG_VID, USBJTAG_PID, 0);
    if (! hiddev) {
        fprintf (stderr, "USB JTAG adapter not found\n");
        return 0;
    }
    a = calloc (1, sizeof (*a));
    if (! a) {
        fprintf (stderr, "Out of memory\n");
        return 0;
    }
    a->hiddev = hiddev;
	a->name = "MRACH PIC USB JTAG v0.20";
    
	// TODO: Set default clock rate.
    //int khz = 500;
    //usbjtag_speed (a, khz);
    /* Activate LED. */
    //usbjtag_reset (a, 0, 0, 1);

    unsigned idcode;
    idcode = usbjtag_GetDeviceId(a);
	if (debug_level > 0)
	   fprintf (stderr, "%s: IDCODE=%08x\n",a->name, idcode);
    if ((idcode & 0xfff) != 0x053) {
        /* Microchip vendor ID is expected. */
        if (debug_level > 0 || (idcode != 0 && idcode != 0xffffffff))
            fprintf (stderr, "%s: incompatible CPU detected, IDCODE=%08x\n",a->name, idcode);
        usbjtag_reset(a, 0, 0, 0);
failed:
        // TODO: close (a->devfd);
        free (a);
        return 0;
    }
/* Check status. */
/* Activate /SYSRST and LED. */
//    usbjtag_reset (a, 0, 1, 1);
    //mdelay (10);
//    usbjtag_send (a, 1, 1, 5, TAP_SW_MTAP, 0);    /* Send command. */
//    usbjtag_send (a, 1, 1, 5, MTAP_COMMAND, 0);   /* Send command. */
//if (memcmp(a->adapter.family_name, "mz", 2) != 0)
//  usbjtag_send (a, 0, 0, 8, MCHP_FLASH_ENABLE, 0);  /* Xfer data. */
//    usbjtag_send (a, 0, 0, 8, MCHP_STATUS, 1);    /* Xfer data. */
    usbjtag_EnterPgm(a);
	unsigned status = usbjtag_GetMchpStatus(a);
	
    if (debug_level > 0)
        fprintf (stderr, "%s: status %04x\n", a->name, status);
    if ((status & ~MCHP_STATUS_DEVRST) != (MCHP_STATUS_CPS | MCHP_STATUS_CFGRDY | MCHP_STATUS_FAEN)) {
        fprintf (stderr, "%s: invalid status = %04x\n", a->name, status);
        usbjtag_reset (a, 0, 0, 0);
        goto failed;
    }
    printf ("      Adapter: %s\n", a->name);
	
	a->adapter.flags = AD_PROBE | AD_ERASE | AD_READ | AD_WRITE;
    /* User functions. */
    a->adapter.close = usbjtag_close;
    a->adapter.get_idcode = usbjtag_get_idcode;
    a->adapter.load_executive = usbjtag_load_executive;
    a->adapter.read_word = usbjtag_read_word;
    a->adapter.read_data = usbjtag_read_data;
    a->adapter.verify_data = usbjtag_verify_data;
    a->adapter.erase_chip = usbjtag_erase_chip;
    a->adapter.program_word = usbjtag_program_word;
    a->adapter.program_row = usbjtag_program_row;
	a->use_executive = 1;
    return &a->adapter;
}

