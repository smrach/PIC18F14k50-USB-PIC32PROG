#include "HardwareProfile.h"
#include "pic32prog.h"
#if defined(__18F14K50)
	#include <p18f14k50.h>
#elif defined(__18F2550)
	#include <p18f2550.h>
#endif 
static UINT8 _inPgmMode = 0;
static UINT8 _wireMode = WIRES_JTAG;
static UINT32 _dummyRead = 0;

UINT8 GetWiresMode(){
	return _wireMode;
}
void SetWiresMode(UINT8 wiresMode){
	_wireMode = wiresMode;
}
void SetupIOPorts(UINT8 setUnset){
	if (_wireMode == WIRES_JTAG){
		if (setUnset==1) {
			Init_JTAG();
			LED_TRIS = OUTPUT_PIN;
		} else {
			DeInit_JTAG_IO();
			LED_TRIS = INPUT_PIN;
		}
	}
	if (_wireMode == WIRES_ICSP){
	    if (setUnset==1) {
			Init_ICSP();
			LED_TRIS = OUTPUT_PIN;
		} else {
			DeInit_ICSP_IO();
			LED_TRIS = INPUT_PIN;
		}
	}
}
void EnterPgmMode(void){
	UINT8 init[] = { 0xB2, 0xC2, 0x12, 0x0A };
	UINT8 bitsNum = 0;
	UINT8 i = 0;
	if (_wireMode == WIRES_JTAG) {
		MCLR = 0; //MCLR LOW (CPU In Reset);
		SendCommand(MTAP_SW_ETAP);
		SendCommand(ETAP_EJTAGBOOT);
		MCLR = 1; //MCLR HIGH (CPU running);
	}  
	if (_wireMode == WIRES_ICSP){
		PGD_OUTPUT(); // Output
		PMCLR = 0;
		PGD_WRITE = 0;
		PGC_LOW();
		//DelayUs(1);
		PMCLR = 1;
		DelayUs(1);
		PMCLR = 0;
		//ICSP_RawWrite(4,(UINT8*)&init); 
		for (i=0;i<4;i++){
			for (bitsNum=0;bitsNum<8;bitsNum++){
				PGD_WRITE = init[i] & 1;
				PGC_HIGH();
				PGC_LOW();
				init[i] >>= 1;	
			}
		}
		DelayUs(5);
		PMCLR = 1;
		DelayUs(5);
		SetMode(ETAP_RESET);
	}
	LED_ON();
	_inPgmMode = 1;
}
void ExitPgmMode(void){
	if (_wireMode == WIRES_JTAG) {
		SetMode(PIC32_RESET); 	//ETAP_RESET SHORT
		MCLR = 0; 				//MCLR ATTIVO (In Reset);
	}  
	if (_wireMode == WIRES_ICSP){
		SetMode(PIC32_RESET);
		PGD_TRIS = 0; // Output
		PGD_WRITE = 0;
		PGC = 0;
		PMCLR = 0;
	}
	LED_OFF();
	_inPgmMode = 0;
}
/* Ciclo di Clock + lettura TDO.
 based on _wireMode do 2-4 phases or normal on phase clock.
*/
UINT8 io_clock_bit(UINT8 tms,UINT8 tdi) {
	UINT8 toRet = 0;
	if (_wireMode == WIRES_JTAG) { //4-wires mode.
		TDI = tdi&0x01;
		TMS = tms&0x01;
		TCK_HIGH();
		toRet = TDO;	//L'uscita è stabile dopo TOT tempo da clock Alto.
		TCK_LOW();
	} else 
	if (_wireMode == WIRES_ICSP){ // 2-wire to 4-wire communication.
/* OUTPUT MODE */		
		PGD_OUTPUT();
		/* PHASE 1 */
		/* TDI OUTPUT */
		PGD_WRITE = (tdi & 0x01);
		PGC_HIGH();
		PGC_LOW();
		
		/* PHASE 2 */
		/* TMS OUTPUT */
		PGD_WRITE = (tms & 0x01);
		PGC_HIGH();
		PGC_LOW();

/* INPUT MODE */
		PGD_WRITE = 0;
		PGD_INPUT();	

		/* PHASE 3 */
		/* DUMMY Clock */
		toRet = PGD_READ;
		PGC_HIGH();
		PGC_LOW();

		/* PHASE 4 */
		/* TDO INPUT (0 or 1) */
		PGC_HIGH();
		toRet = PGD_READ;	
		PGC_LOW();
	}
	return toRet;
}
/* SetMode(mode)
PAGE 13 DS60001145N
PSEUDO OPERATIONS  

ex mode 6'b011111'

TCK-> _/ \_/ \_/ \_/ \_/ \_/ \_ 

TMS-> /  1   1   1   1   1\_0__

TDI->\__________________________

TDO->----------------------------
*/
void SetMode(UINT8 mode, UINT8 nBits){
	while (nBits--)	{
		io_clock_bit(mode,0);
		mode >>= 1;
	}
}
/* SendCommand(command)
PAGE 14 DS60001145N

TMS Header (1100)
+
nbits command + (TMS=1 on last bit)
+
TMS Footer (10)
*/
void SendCommand(UINT8 cmd, UINT8 nCmdBits){
	/*1)TMS Header (1100) 
	    ENTER SHIFT-IR STATE */
	io_clock_bit(1, 0);		/* SELECT-DR		*/
	io_clock_bit(1, 0);		/* SELECT-IR		*/
	io_clock_bit(0, 0);		/* CAPTURE-IR		*/
	io_clock_bit(0, 0);		/* SHIFT-IR		*/
	while (nCmdBits--){	
		io_clock_bit(!nCmdBits ? 1 : 0, cmd);
		cmd>>=1;
	}
	/*3)TMS Footer (10) 
	    ENTER RUN-TEST/IDLE STATE */ 
	io_clock_bit(1, 0);		/* UPDATE-IR		*/
	io_clock_bit(0, 0);		/* RUN-TEST/IDLE 	*/
}
/* XferData Pseudo Operation
PAGE 15 DS60001145N
Format: oData = XferData (iData)
TSM Header (100)
+
+
TSM Footer (10);
*/
void XferData(UINT8* data, UINT8 nBits, UINT8* response){
	UINT8 tdoBits = 0;
	UINT8 tdiBits = 0;
//1. TMS Header (100)  +Read.
	io_clock_bit(1,0);
	io_clock_bit(0,0);
//2.a JTAG clock
	if (_wireMode == WIRES_JTAG) {
		io_clock_bit(0,0);
	} else {
//2.b ICSP clock
		*response = io_clock_bit(0,0);
		tdoBits++;
	}
//3. Send and Receive Data
	while (nBits--){
		*response |= io_clock_bit((!nBits)?1:0,*data)<<tdoBits;
		/**data >>= 1;
		bitsNum++;
		if (bitsNum == 8) {
			bitsNum = 0;
			data++;
			response++;
		}*/
		tdoBits++;
		if (tdoBits == 8) {
			tdoBits = 0;
			response++;
		}
		*data >>= 1;
		tdiBits++;
		if (tdiBits == 8) {
			tdiBits = 0;
			data++;
		} 
	}
//4. TMS Footer (10) ICSP_SetMode(0x01,2);//TMS Footer = 10 => 01 
	io_clock_bit(1,0);
	io_clock_bit(0,0);
}
/* XferFastData Pseudo Operation
*/
void XferFastData(UINT8* data, UINT8* response, UINT8* prAcc){
	UINT8 nBits = 32;
	UINT8 tdoBits = 0;
	UINT8 tdiBits = 0;
//1. TMS Header (100)  +Read.
	io_clock_bit(1,0);
	io_clock_bit(0,0);
	if (_wireMode == WIRES_JTAG) {
		io_clock_bit(0,0);
	} else {
		*response = io_clock_bit(0,0);
		tdoBits++;
	}
//2. oPrAcc bit.
	*prAcc = io_clock_bit(0,0);
//3. Send and Receive Data
	while (nBits--){
		*response |= io_clock_bit((!nBits)?1:0,*data)<<tdoBits;
		tdoBits++;
		if (tdoBits == 8) {
			tdoBits = 0;
			response++;
		}
		*data >>= 1;
		tdiBits++;
		if (tdiBits == 8) {
			tdiBits = 0;
			data++;
		} 
	}
//4. TMS Footer (10) ICSP_SetMode(0x01,2);//TMS Footer = 10 => 01 
	io_clock_bit(1,0);
	io_clock_bit(0,0);
}
/* Get DeviceId 
*/
void GetDeviceId(UINT8* readValue) {
	SetMode(ETAP_RESET);
	XferData((UINT8*)0,32,readValue);
}
/* CHECK DEVICE STATUS MCHP_STATUS
   PAGE 20 DS60001145N 
 1. SetMode (6’b011111) to force the Chip TAP controller into Run Test/Idle state.
 2. SendCommand (MTAP_SW_MTAP).
 3. SendCommand (MTAP_COMMAND).
 4. statusVal = XferData (MCHP_STATUS).
 5. If CFGRDY (statusVal<3>) is not ‘1’ and FCBUSY (statusVal<2>) is not ‘0’, GOTO step 4.
*/
void GetMCHPStatus(UINT8* readStatus){
   if (_wireMode == WIRES_JTAG ) {
		MCLR=0;
   }
   SetMode(ETAP_RESET);
   SendCommand(MTAP_SW_MTAP); //MTAP_SW_MTAP 	0x04,5 (00100)
   SetMode(ETAP_RESET);
   SendCommand(MTAP_COMMAND); //MTAP_COMMAND 	0x07,5 (00111)
   XferData((UINT8*)MCHP_STATUS,readStatus); //
}
/* Enter Serial Execution Mode
*/
UINT8 SerialExecutionMode(UINT8 mx){
	UINT8 status, dummyRead; 
	UINT8 cmd = 0;
	if (!_inPgmMode)
	{
		EnterPgmMode();
	}
	GetMCHPStatus((UINT8*)&status);
	//chekc if not write protected
	if (status & 0x80) { 
		if (_wireMode == WIRES_ICSP) {			
			cmd = 0xD1;					//MCHP_ASSERT_RST
			XferData((UINT8*)&cmd,8,(UINT8*)&dummyRead);
			SendCommand(MTAP_SW_ETAP);
			//SetMode(ETAP_RESET);
			SendCommand(ETAP_EJTAGBOOT);
			SendCommand(MTAP_SW_MTAP);
		    //SetMode(ETAP_RESET);
		    SendCommand(MTAP_COMMAND);
			cmd = 0xD0;					//MCHP_DEASSERT_RST
		    XferData((UINT8*)&cmd,8,(UINT8*)&dummyRead);
			if (mx) {
				cmd = 0xFE;				//MCHP_FLASH_ENABLE
				XferData((UINT8*)&cmd,8,(UINT8*)&dummyRead);
			}
		    SendCommand(MTAP_SW_ETAP);
		    //SetMode(ETAP_RESET);
		}  
		if (_wireMode == WIRES_JTAG) {
			SendCommand(MTAP_SW_ETAP);
			SetMode(ETAP_RESET);
			SendCommand(ETAP_EJTAGBOOT);
			MCLR = 1;
		}
	} else {
		return status;
	}
	return 0x80;//MCHP_STATUS_CPS;
}
/* XferInstruction Pseudo Operation
   Checking ETAP_READY Flags
*/
UINT8 XferInstruction(UINT32 instrData){
	UINT32 instrCode = 0x0000C000;
	if (!WaitETAP_Ready(150)) 
		return 0;
	SendCommand(ETAP_DATA);
	XferData((UINT8*)&instrData,32,(UINT8*)&_dummyRead);		//XferData(instrData,32);
	SendCommand(ETAP_CONTROL);
	XferData((UINT8*)&instrCode,32,(UINT8*)&_dummyRead); 		//XferData(0x0000C000,32);
	return 1;
}
/* Wait for ETAP_READY Flag (iterating retryCounts times)
	Return : 1 if ETAP_READY / 0 if FAIL
*/
BYTE WaitETAP_Ready(UINT8 retryCounts){
	UINT32 instrCode = 0x0004C000;
	UINT32 readVal = 0 ;
	SendCommand(ETAP_CONTROL);
	do{
		XferData((UINT8*)&instrCode,32,(UINT8*)&readVal);
		if (readVal & PIC32_ECONTROL_PRACC)
			return 1;
		DelayUs(1);
	} while (retryCounts--);
	return 0;
}
void DelayUs( int us ) {
	int _iuTemp = us * 12;
	while(_iuTemp--)
		Nop();
}
UINT32_VAL ReadFromAddress(UINT32 address){
	UINT32_VAL tmpValue;
	UINT32_VAL instrCode; //  UINT16 LW; UINT16 HW;;
	tmpValue.Val = address; //park value for helper instruction merge..
	// Load Fast Data register address to s3
	//instruction = 0x3c130000;
	//instruction |= (0xff200000>>16)&0x0000ffff;
	instrCode.Val = 0x3c13ff20;
	XferInstruction(instrCode.Val); // lui s3, <FAST_DATA_REG_ADDRESS(31:16)> - set address of fast
    // Load memory address to be read into t0
	// instruction = 0x3c080000;
	// instruction |= (address>>16)&0x0000ffff;
	instrCode.word.HW = 0x3c08;
	instrCode.word.LW = tmpValue.word.HW;
    XferInstruction(instrCode.Val); // lui t0, <DATA_ADDRESS(31:16)> - set address of data
	//instruction = 0x35080000;
	//instruction |= (address&0x0000ffff);
	instrCode.word.HW = 0x3508;
	instrCode.word.LW = tmpValue.word.LW;
	XferInstruction(instrCode.Val); // ori t0, <DATA_ADDRESS(15:0)> - set address of data
	// Read data
	XferInstruction(0x8d090000); // lw t1, 0(t0)
	// Store data into Fast Data register
	XferInstruction(0xae690000); // sw t1, 0(s3) - store data to fast data register
	XferInstruction(0); // nop
	SendCommand(ETAP_FASTDATA);
	XferFastData((UINT8*)0,(UINT8*)32, &tmpValue.v[0]);
}
void GetPEResponse(UINT8* response){
	UINT32 instrCode = 0x0000C000;
	WaitETAP_Ready(150);
	SendCommand(ETAP_DATA);
	XferData((UINT8*)0,32,response);
	SendCommand(ETAP_CONTROL);
	XferData((UINT8*)&instrCode,32,(UINT8*)&_dummyRead);
}