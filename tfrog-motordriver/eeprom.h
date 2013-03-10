#ifndef __EEPROM_H__
#define __EEPROM_H__

void msleep( int ms );
void EEPROM_Init(  );
int EEPROM_Read( int addr, void *data, int len );
int EEPROM_Write( int addr, void *data, int len );


typedef struct _Tfrog_EEPROM_data
{
	int key;
	int serial_no;
	char robot_name[32];
	unsigned short PWM_resolution;
	unsigned short PWM_deadtime;
} Tfrog_EEPROM_data;

#define TFROG_EEPROM_ROBOTPARAM_ADDR  0x100
#define TFROG_EEPROM_KEY              0x00AA77CC
#define TFROG_EEPROM_DEFAULT \
{\
	TFROG_EEPROM_KEY,\
	0x01300000,\
	{"unknown"},\
	1200,\
	20\
}

#endif

