#ifndef H
#define H

#define MTAP_IDCODE  	0x01,5
#define MTAP_SW_MTAP 	0x04,5
#define MTAP_SW_ETAP 	0x05,5
#define MTAP_COMMAND 	0x07,5

#define ETAP_ADDRESS 	0x08,5
#define ETAP_DATA    	0x09,5
#define ETAP_CONTROL 	0x0A,5
#define ETAP_EJTAGBOOT 	0x0C,5
#define ETAP_FASTDATA 	0x0E,5
#define ETAP_RESET		0x1F,6
#define PIC32_RESET		0x1F,5	/* ENTER TEST-LOGIC-RESET STATE */

#define MCHP_STATUS        0x00,8
#define MCHP_ASSERT_RST    0x01,8
#define MCHP_DE_ASSERT_RST 0x00,8
#define MCHP_ERASE         0xFC,8
#define MCHP_FLASH_ENABLE  0xFE,8
#define MCHP_FLASH_DISABLE 0xFD,8
#define MCHP_READ_CONFIG   0xFF,8
/*
 * EJTAG Control register.
 */
#define CONTROL_ROCC            (1 << 31)   /* Reset occured */
#define CONTROL_PSZ_MASK        (3 << 29)   /* Size of pending access */
#define CONTROL_PSZ_BYTE        (0 << 29)   /* Byte */
#define CONTROL_PSZ_HALFWORD    (1 << 29)   /* Half-word */
#define CONTROL_PSZ_WORD        (2 << 29)   /* Word */
#define CONTROL_PSZ_TRIPLE      (3 << 29)   /* Triple, double-word */
#define CONTROL_VPED            (1 << 23)   /* VPE disabled */
#define CONTROL_DOZE            (1 << 22)   /* Processor in low-power mode */
#define CONTROL_HALT            (1 << 21)   /* System bus clock stopped */
#define CONTROL_PERRST          (1 << 20)   /* Peripheral reset applied */
#define CONTROL_PRNW            (1 << 19)   /* Store access */
//#define CONTROL_PRACC           (1 << 18)   /* Pending processor access */
#define PIC32_ECONTROL_PRACC    (0x00040000) /* PROCESSOR ACCESS */
#define CONTROL_RDVEC           (1 << 17)   /* Relocatable debug exception vector */
#define CONTROL_PRRST           (1 << 16)   /* Processor reset applied */
#define CONTROL_PROBEN          (1 << 15)   /* Probe will service processor accesses */
#define CONTROL_PROBTRAP        (1 << 14)   /* Debug vector at ff200200 */
#define CONTROL_EJTAGBRK        (1 << 12)   /* Debug interrupt exception */
#define CONTROL_DM              (1 << 3)    /* Debug mode */

/*
 * PE command set.
 */
#define PE_ROW_PROGRAM          0x0     /* Program one row of flash memory */
#define PE_READ                 0x1     /* Read N 32-bit words */
#define PE_PROGRAM              0x2     /* Program flash memory */
#define PE_WORD_PROGRAM         0x3     /* Program one word of flash memory */
#define PE_CHIP_ERASE           0x4     /* Erase the entire chip */
#define PE_PAGE_ERASE           0x5     /* Erase pages by address */
#define PE_BLANK_CHECK          0x6     /* Check blank memory */
#define PE_EXEC_VERSION         0x7     /* Read the PE software version */
#define PE_GET_CRC              0x8     /* Get the checksum of memory */
#define PE_PROGRAM_CLUSTER      0x9     /* Program N bytes */
#define PE_GET_DEVICEID         0xA     /* Return the hardware ID of device */
#define PE_CHANGE_CFG           0xB     /* Change PE settings */

#include <GenericTypeDefs.h>
UINT8 GetWiresMode(void);
void SetWiresMode(UINT8 wiresMode);
void SetupIOPorts(UINT8 setUnset);
void EnterPgmMode(void);
void ExitPgmMode(void);
UINT8 io_clock_bit(UINT8 tms,UINT8 tdi); 
//prototypes jtag...
void SetMode(UINT8 mode, UINT8 nBits);
void SendCommand(UINT8 cmd, UINT8 ncmdBits);
void XferData(UINT8* data, UINT8 nBits,  UINT8* response);
void XferFastData(UINT8* data, UINT8* response, UINT8* prAcc);
void GetMCHPStatus(UINT8* status);
void GetDeviceId(UINT8* readValue);
UINT8 XferInstruction(UINT32 instrData);/* Boolean response. 0 = fail or timeout 1 = success */
BYTE WaitETAP_Ready(UINT8 retryCounts);
UINT8 SerialExecutionMode(UINT8 mx);
UINT32_VAL ReadFromAddress(UINT32 address);
void DelayUs( int us );
void GetPEResponse(UINT8* response);
#endif