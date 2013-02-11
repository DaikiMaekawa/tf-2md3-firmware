// -----------------------------------------------------------------------------
// Headers
// ------------------------------------------------------------------------------

#include <board.h>
#include <pio/pio.h>
#include <pio/pio_it.h>
#include <aic/aic.h>
#include <tc/tc.h>
#include <utility/trace.h>
#include <utility/led.h>
#include <usb/device/cdc-serial/CDCDSerialDriver.h>
#include <usb/device/cdc-serial/CDCDSerialDriverDescriptors.h>
#include <string.h>

#include "controlPWM.h"
#include "controlVelocity.h"
#include "registerFPGA.h"

MotorState motor[2];
MotorParam motor_param[2];
DriverParam driver_param;

extern int watchdog;

static const Pin pinsLeds[] = { PINS_LEDS };

static const unsigned int numLeds = PIO_LISTSIZE( pinsLeds );

// / PWM Enable pin instance.
static const Pin pinPWMEnable = PIN_PWM_ENABLE;

// ------------------------------------------------------------------------------
// / Velocity control loop (1ms)
// ------------------------------------------------------------------------------
void ISR_VelocityControl(  )
{
	// volatile unsigned int status;
	static int pwm_sum[2] = { 0, 0 };
	static int i;

	// PIO_Clear(&pinsLeds[USBD_LEDOTHER]);

	// status = AT91C_BASE_TC0->TC_SR;

	if( driver_param.enable_watchdog )
	{
		driver_param.watchdog++;

		if( driver_param.watchdog == driver_param.watchdog_limit )
		{
			driver_param.enable_watchdog = 0;
			driver_param.cnt_updated = 0;
			driver_param.watchdog_limit = 600;
			driver_param.servo_level = SERVO_LEVEL_STOP;
			driver_param.admask = 0;
			driver_param.io_mask = 0;

			motor[0].pos = motor[1].pos = 0;
			motor_param[0].enc_rev = 0;
			motor_param[1].enc_rev = 0;
			motor_param[0].motor_type = MOTOR_TYPE_AC3;
			motor_param[1].motor_type = MOTOR_TYPE_AC3;
			if( *( int * )( 0x0017FF00 + sizeof ( driver_param ) + sizeof ( motor_param ) ) == 0xAACC )
			{
				memcpy( &driver_param, ( int * )( 0x0017FF00 ), sizeof ( driver_param ) );
				memcpy( motor_param, ( int * )( 0x0017FF00 + sizeof ( driver_param ) ), sizeof ( motor_param ) );
			}

			THEVA.GENERAL.PWM.COUNT_ENABLE = 0;
			THEVA.GENERAL.OUTPUT_ENABLE = 0;
			PIO_Set( &pinPWMEnable );
			return;
			// AIC_DisableIT(AT91C_ID_TC0);
		}
	}

	if( driver_param.servo_level >= SERVO_LEVEL_TORQUE )
	{											// servo_level 2(toque enable)
		static int toq[2], out_pwm[2];

		if( driver_param.servo_level >= SERVO_LEVEL_VELOCITY )
		{										// servo_level 3 (speed enable)

			static int toq_pi[2], s_a, s_b;
			for( i = 0; i < 2; i++ )
			{
				motor[i].ref.vel_interval++;
				if( motor[i].ref.vel_changed )
				{
					static int vel_buf[2] = { 0, 0 };
					motor[i].ref.vel_buf = motor[i].ref.vel;
					motor[i].ref.vel_diff = ( motor[i].ref.vel_buf - vel_buf[i] ) / motor[i].ref.vel_interval;

					vel_buf[i] = motor[i].ref.vel_buf;
					motor[i].ref.vel_interval = 0;

					motor[i].ref.vel_changed = 0;
				}

				// 積分
				motor[i].error = motor[i].ref.vel_buf - motor[i].vel;
				motor[i].error_integ += motor[i].error;
				if( motor[i].error_integ > driver_param.integ_max )
				{
					motor[i].error_integ = driver_param.integ_max;
				}
				else if( motor[i].error_integ < driver_param.integ_min )
				{
					motor[i].error_integ = driver_param.integ_min;
				}

				// PI制御分
				toq_pi[i]  = motor[i].error * motor_param[i].Kp;
				toq_pi[i] += motor[i].error_integ * motor_param[i].Ki;
			}

			// PWSでの相互の影響を考慮したフィードフォワード
			s_a = ( toq_pi[0] + motor[0].ref.vel_diff ) / 16;
			s_b = ( toq_pi[1] + motor[1].ref.vel_diff ) / 16;

			toq[0] = ( s_a * driver_param.Kdynamics[0]
					   + s_b * driver_param.Kdynamics[2] + motor[0].ref.vel_buf * driver_param.Kdynamics[4] ) / 256;
			toq[1] = ( s_b * driver_param.Kdynamics[1]
					   + s_a * driver_param.Kdynamics[3] + motor[1].ref.vel_buf * driver_param.Kdynamics[5] ) / 256;
		}
		else
		{										// servo_level 2(toque enable)
			toq[0] = 0;
			toq[1] = 0;
		}
		// 出力段
		for( i = 0; i < 2; i++ )
		{
			// トルクでクリッピング
			if( toq[i] >= motor_param[i].torque_max )
			{
				toq[i] = motor_param[i].torque_max;
			}
			if( toq[i] <= motor_param[i].torque_min )
			{
				toq[i] = motor_param[i].torque_min;
			}

			// 摩擦補償（線形）
			if( motor[i].vel > 0 )
			{
				toq[i] += ( motor_param[i].fr_wplus * motor[i].vel / 16 + motor_param[i].fr_plus );
			}
			else if( motor[i].vel < 0 )
			{
				toq[i] -= ( motor_param[i].fr_wminus * ( -motor[i].vel ) / 16 + motor_param[i].fr_minus );
			}
			// トルク補償
			toq[i] += motor_param[i].torque_offset;

			// トルクでクリッピング
			if( toq[i] >= motor_param[i].torque_limit )
			{
				toq[i] = motor_param[i].torque_limit;
			}
			if( toq[i] <= -motor_param[i].torque_limit )
			{
				toq[i] = -motor_param[i].torque_limit;
			}

			// トルク→pwm変換
			if( motor[i].dir == 0 )
			{
				out_pwm[i]  = 0;
			}
			else
			{
				out_pwm[i]  = ( motor[i].vel * motor_param[i].Kvolt ) / 16;
			}
			motor[i].ref.torque = toq[i] * motor_param[i].Kcurrent;
			out_pwm[i] += motor[i].ref.torque;
			out_pwm[i] /= 65536;

			// PWMでクリッピング
			if( out_pwm[i] > driver_param.PWM_max - 1 )
				out_pwm[i] = driver_param.PWM_max - 1;
			if( out_pwm[i] < driver_param.PWM_min + 1 )
				out_pwm[i] = driver_param.PWM_min + 1;
		}

		// 出力
		motor[0].ref.rate = out_pwm[0];
		motor[1].ref.rate = out_pwm[1];

		pwm_sum[0] += out_pwm[0];
		pwm_sum[1] += out_pwm[1];

		driver_param.cnt_updated++;
		if( driver_param.cnt_updated == 5 )
		{
			// static long cnt = 0;
			motor[0].ref.rate_buf = pwm_sum[0];
			motor[1].ref.rate_buf = pwm_sum[1];
			pwm_sum[0] = 0;
			pwm_sum[1] = 0;
		}
	}											// servo_level 2*/
	else
	{
		motor[0].ref.rate = 0;
		motor[1].ref.rate = 0;
	}
	// PIO_Set(&pinsLeds[USBD_LEDOTHER]);
}

// ------------------------------------------------------------------------------
// / Configure velocity control loop
// ------------------------------------------------------------------------------
inline void controlVelocity_init(  )
{
	// Configure timer 0
	/* 
	 * AT91C_BASE_PMC->PMC_PCER = (1 << AT91C_ID_TC0); AT91C_BASE_TC0->TC_CCR = AT91C_TC_CLKDIS; AT91C_BASE_TC0->TC_IDR 
	 * = 0xFFFFFFFF; AT91C_BASE_TC0->TC_CMR = AT91C_TC_CLKS_TIMER_DIV3_CLOCK | AT91C_TC_WAVESEL_UP_AUTO |
	 * AT91C_TC_WAVE; AT91C_BASE_TC0->TC_RC = 1500 / 8; // 1ms 1500 AT91C_BASE_TC0->TC_IER = AT91C_TC_CPCS;
	 * 
	 * AIC_ConfigureIT(AT91C_ID_TC0, 1, ISR_VelocityControl); //AIC_EnableIT(AT91C_ID_TC0);
	 * 
	 * AT91C_BASE_TC0->TC_CCR = AT91C_TC_CLKEN | AT91C_TC_SWTRG; */
}
