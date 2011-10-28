/* ----------------------------------------------------------------------------
 *         ATMEL Microcontroller Software Support 
 * ----------------------------------------------------------------------------
 * Copyright (c) 2008, Atmel Corporation
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Atmel's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ----------------------------------------------------------------------------
 */


//-----------------------------------------------------------------------------
//         Headers
//------------------------------------------------------------------------------

#include <board.h>
#include <pio/pio.h>
#include <pio/pio_it.h>
#include <aic/aic.h>
#include <tc/tc.h>
#include <usart/usart.h>
#include <utility/trace.h>
#include <string.h>
#include <utility/led.h>
#include <usb/device/cdc-serial/CDCDSerialDriver.h>
#include <usb/device/cdc-serial/CDCDSerialDriverDescriptors.h>
#include <pmc/pmc.h>

#include "power.h"
#include "controlPWM.h"
#include "controlVelocity.h"
#include "registerFPGA.h"
#include "communication.h"


extern int getStackPointer( void );
extern int getIrqStackPointer( void );

static const Pin pinsLeds[] = {PINS_LEDS};
static const unsigned int numLeds = PIO_LISTSIZE(pinsLeds);

//------------------------------------------------------------------------------
//      Definitions
//------------------------------------------------------------------------------

/// Size in bytes of the buffer used for reading data from the USB & USART
#define DATABUFFERSIZE \
    BOARD_USB_ENDPOINTS_MAXPACKETSIZE(CDCDSerialDriverDescriptors_DATAIN)


//------------------------------------------------------------------------------
//      Internal variables
//------------------------------------------------------------------------------

char connecting = 0;

/// List of pins that must be configured for use by the application.
static const Pin pins[] = { PIN_PWM_ENABLE };

/// VBus pin instance.
static const Pin pinVbus = PIN_USB_VBUS;

/// PWM Enable pin instance.
const Pin pinPWMEnable = PIN_PWM_ENABLE;

/// Buffer for storing incoming USB data.
static unsigned char usbBuffer[DATABUFFERSIZE];


//------------------------------------------------------------------------------
//          Main
//------------------------------------------------------------------------------

void SRAM_Init()
{
    static const Pin pinsSram[] = {PINS_SRAM};
    
    // Enable corresponding PIOs
    PIO_Configure(pinsSram, PIO_LISTSIZE(pinsSram));
    
	AT91C_BASE_SMC->SMC2_CSR[0] = 1 | AT91C_SMC2_WSEN | (0 << 8) | AT91C_SMC2_BAT | AT91C_SMC2_DBW_16 | (1 << 24) | (1 << 28);
}

//------------------------------------------------------------------------------
/// Handles interrupts coming from PIO controllers.
//------------------------------------------------------------------------------
static void ISR_Vbus(const Pin *pPin)
{
    // Check current level on VBus
    if ( PIO_Get(&pinVbus)) {
        TRACE_INFO("VBUS conn\n\r");
        USBD_Connect();
		connecting = 1;
		//LED_Set(USBD_LEDPOWER);
    }else{
        TRACE_INFO("VBUS discon\n\r");
        USBD_Disconnect();
		//LED_Clear(USBD_LEDPOWER);
		//LED_Clear(USBD_LEDUSB);
		PIO_Set( &pinPWMEnable );
    }
}

//------------------------------------------------------------------------------
/// Configures the VBus pin to trigger an interrupt when the level on that pin
/// changes.
//------------------------------------------------------------------------------
static void VBus_Configure( void )
{
    TRACE_INFO("VBus configuration\n\r");

    // Configure PIO
    PIO_Configure(&pinVbus, 1);
    PIO_ConfigureIt(&pinVbus, ISR_Vbus);
    PIO_EnableIt(&pinVbus);

    ISR_Vbus(&pinVbus);   
}

int natoi( unsigned char *buf, int size )
{
	int ret, i;
	ret = 0;
	for( i = 0; i < size; i ++ ){
		if( '0' <= *buf && *buf <= '9' )
		{
			ret *= 16;
			ret += *buf - '0';
		}
		else if( 'A' <= *buf && *buf <= 'F' )
		{
			ret *= 16;
			ret += *buf - 'A' + 0xA;
		}
		buf ++;
	}
	return ret;
}
int nitoa( unsigned char *buf, int data, int len )
{
	int i;
	for( i = 0; i < len; i ++ ){
		*buf = ( ( (unsigned int)data >> ( ( len - i - 1 ) * 4 ) ) & 0xF ) + '0';
		if( *buf > '9' ){
			*buf = *buf - '9' - 1 + 'A';
		}
		buf ++;
	}
	return len;
}


//------------------------------------------------------------------------------
/// Callback invoked when data has been received on the USB.
//------------------------------------------------------------------------------
static void UsbDataReceived(unsigned int unused,
                            unsigned char status,
                            unsigned int received,
                            unsigned int remaining)
{
    // Check that data has been received successfully
    if (status == USBD_STATUS_SUCCESS) {
		static int remain = 0;
/*
		unsigned short *freg = (void*)0x10000000;
		unsigned short data;
		int addr;

		addr = 0;
		if( received > 2 ){
			addr = natoi( usbBuffer, 2 );
			if( received > 7 ){
				data = natoi( usbBuffer + 3, 4 );
				freg[ addr ] = data;
				usbBuffer[ received ++ ] = 'W';
			}
			usbBuffer[ received ++ ] = '[';
			received += nitoa( usbBuffer + received, freg[ addr ], 4 );
			usbBuffer[ received ++ ] = ']';
			usbBuffer[ received ++ ] = '\n';
			usbBuffer[ received ++ ] = '\n';
		}
*/
/*
		if( received > 1 ){
			motor[0].ref.vel = natoi( usbBuffer, received - 1 );
		}
		received += nitoa( usbBuffer + received, motor[0].vel, 4 );
		usbBuffer[ received ++ ] = '\n';
		usbBuffer[ received ++ ] = '\n';
*/
		//LED_Clear(USBD_LEDUSB);
		remain = data_fetch( usbBuffer, received + remain );

	    CDCDSerialDriver_Read(usbBuffer + remain,
	                          DATABUFFERSIZE - remain,
	                          (TransferCallback) UsbDataReceived,
	                          0);
		//LED_Set(USBD_LEDUSB);
/*
            TRACE_ERROR(
                      "%d %d\n",
                      driver_param.watchdog, THEVA.GENERAL.PWM.COUNT_ENABLE);
*/
        // Check if bytes have been discarded
        if ((received == DATABUFFERSIZE) && (remaining > 0)) {

            TRACE_WARNING(
                      "UsbDataReceived: %u bytes discarded\n\r",
                      remaining);
        }
    }
    else {

        TRACE_WARNING( "UsbDataReceived: Transfer error\n\r");
    }
}


//------------------------------------------------------------------------------
//          Main
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
/// Initializes drivers and start the USB <-> Serial bridge.
//------------------------------------------------------------------------------
int main()
{
	short analog[9];
	short enc_buf2[2];

    TRACE_CONFIGURE(DBGU_STANDARD, 230400, BOARD_MCK);
    printf("-- Locomotion Board %s --\n\r", SOFTPACK_VERSION);
    printf("-- %s\n\r", BOARD_NAME);
    printf("-- Compiled: %s %s --\n\r", __DATE__, __TIME__);

    // If they are present, configure Vbus & Wake-up pins
    PIO_InitializeInterrupts(0);

    // Configure USART
    PIO_Configure(pins, PIO_LISTSIZE(pins));

	// Disable PWM Output
	PIO_Set( &pinPWMEnable );

    // BOT driver initialization
    CDCDSerialDriver_Initialize();

    LED_Configure(USBD_LEDPOWER);
    LED_Configure(USBD_LEDUSB);
    LED_Configure(USBD_LEDOTHER);

    // connect if needed
    VBus_Configure();
/*
	printf("sizeof(int) = %d\n\r", (int)sizeof(int) );
	printf("sizeof(long int) = %d\n\r", (int)sizeof(long int) );
	printf("sizeof(long) = %d\n\r", (int)sizeof(long) );
	printf("sizeof(short) = %d\n\r", (int)sizeof(short) );
*/
	printf("SRAM init\n\r" );
	SRAM_Init();

	printf("PWM control init\n\r" );
	// Configure PWM control
	controlPWM_init();

	printf("Velocity Control init\n\r" );
    // Configure velocity control loop
	controlVelocity_init();

	enc_buf2[0] = enc_buf2[1] = 0;
	motor[0].pos = motor[1].pos = 0;
	driver_param.cnt_updated = 0;
	driver_param.watchdog = 0;
	driver_param.watchdog_limit = 500;
	driver_param.servo_level = SERVO_LEVEL_STOP;

	motor[0].ref.vel = 0;
	motor[1].ref.vel = 0;
	motor_param[0].enc0 = 0;
	motor_param[1].enc0 = 0;

	motor_param[0].enc_rev = 800;
	motor_param[1].enc_rev = 800;
	
	if( *(int*)( 0x0017FF00 + sizeof(driver_param) + sizeof(motor_param) ) == 0xAACC )
	{
		memcpy( &driver_param, (int*)( 0x0017FF00 ), sizeof(driver_param) );
		memcpy( motor_param, (int*)( 0x0017FF00 + sizeof(driver_param) ), sizeof(motor_param) );
	}
	
	// Watchdog Enable
//	AT91C_BASE_WDTC->WDTC_WDMR = 102 /*0.4s*/ | AT91C_WDTC_WDRSTEN | AT91C_WDTC_WDRPROC | ( 102 << 16 ) | AT91C_WDTC_WDDBGHLT | AT91C_WDTC_WDIDLEHLT;

    //		PIO_Clear(&pinsLeds[USBD_LEDOTHER]);
    //		PIO_Set(&pinsLeds[USBD_LEDOTHER]);
    // Driver loop
    while (1) {
		static int i;
		//int j;

//		AT91C_BASE_WDTC->WDTC_WDCR = 1;
		data_analyze( );
		if( ( i ++ ) % 50000 == 0 )
		{
		//	int i;
/*			printf("SP 0x%x 0x%x\n\r", getStackPointer(), getIrqStackPointer() );
			for( i = 0; i < 2; i ++ )
			{
				printf(" Motor %d: iref=%d vel=%d vref=%d pos=%d(%d)  %d\n\r", 
						i, motor[i].ref.rate, motor[i].vel, motor[i].ref.vel, motor[i].pos, motor_param[i].enc0, motor[i].error_integ );
				printf(" Hall: %d %d %d   %d\n", 
						(THEVA.MOTOR[i].ROT_DETECTER.HALL & 1)!=0, 
						(THEVA.MOTOR[i].ROT_DETECTER.HALL & 2)!=0, 
						(THEVA.MOTOR[i].ROT_DETECTER.HALL & 4)!=0, 
						(THEVA.MOTOR[i].ROT_DETECTER.HALL & 0x80)!=0  );
				for( j = 0; j < 3; j ++ )
				{
					printf( "  %dH:%d L:%d\n",j,THEVA.MOTOR[i].PWM[j].H,THEVA.MOTOR[i].PWM[j].L );
				}
			}
			printf("\n\r" );*/
		}
		if( connecting ){
		    if(USBD_GetState() < USBD_STATE_CONFIGURED) continue;

		    // Start receiving data on the USB
		    CDCDSerialDriver_Read(usbBuffer,
		                          DATABUFFERSIZE,
		                          (TransferCallback) UsbDataReceived,
		                          0);
			connecting = 0;


		}

		if( driver_param.cnt_updated >= 5 )
		{
			unsigned short mask;
			//static long cnt = 0;
			/* 約5msおき */
			
			mask = 0;//analog_mask;
			data_send( ( short )( ( short )motor[0].enc_buf - ( short )enc_buf2[0] ),
							( short )( ( short )motor[1].enc_buf - ( short )enc_buf2[1] ), 
							motor[0].ref.rate_buf, motor[1].ref.rate_buf, analog, mask );

			enc_buf2[0] = motor[0].enc_buf;
			enc_buf2[1] = motor[1].enc_buf;

			driver_param.cnt_updated = 0;

		}
    }
}

