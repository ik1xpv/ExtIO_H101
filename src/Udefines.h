#if !defined UDEFINES_H
#define UDEFINES_H
//
//	Oscar Steila fecit MM, MMXV
//
#include "udefines.h"


#define BITLEN  (48000/4800)   // n sample in a bit period

#define FRAME_SIZE 140

#define MAXRX 256

#define LF	0x0A
#define CR	0x0D
#define SYN 0x16
#define DLE 0x10
#define STX 0x02
#define ETX 0x03
#define SUB 0x1A

#define SYNDLESTX 0x161002

#define AT_INFO				0x41
#define AT_ERASE_ALL	  	0x42
#define AT_WRITEFLASH	  	0x43
#define AT_WRITEEEPROM	  	0x44
#define AT_EXCHANGE  		0x45
#define AT_CMD	   			0x46
#define AT_CONTROLINIT     	0x47
#define ATRV_PASSW_ERR	   	15


// valori per CTRL
#define SADR_MASK	0x03
#define DADR_MASK	0x0c
#define TYPE_MASK	0x30
#define NEXT_FRAME	0x40			// non gestito in questo protocollo
#define ERROR_FLAG	0x80

#define SADR_MCPU 0x0
#define SADR_ACPU 0x3
#define DADR_MCPU 0x0
#define DADR_ACPU 0xC

#define TYPE_RSP	0x00
#define TYPE_IND	0x10
#define TYPE_CMD	0x20
#define TYPE_CNF	0x30

#define CTRL_RX		   	TYPE_CMD + DADR_ACPU + SADR_MCPU   //00


typedef enum{
	ATM_NUL = 0,
	ATM_DDS,
	ATM_IO,
	ATM_RST,
	ATM_L1,
	ATM_L2
} RISCMD;

static unsigned short tabfiltri[] = {  //0- 33 MHz
0x1ff,0x1fe,0x17b, 0xa1, 0x6e,
0x46,  0x36, 0x29, 0x1f, 0x17,
0x13,  0x0f, 0x0c, 0x0b, 0x09,
0x08,   0x7,  0x6,  0x5,  0x4,
0x4,    0x3,  0x3,  0x2,  0x2,
0x1,    0x1,  0x1,  0x0,  0x0,
0x0,    0x0,  0x0,  0x0
};


#endif
