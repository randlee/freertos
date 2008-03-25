/*
	FreeRTOS.org V4.7.2 - Copyright (C) 2003-2008 Richard Barry.

	This file is part of the FreeRTOS.org distribution.

	FreeRTOS.org is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	FreeRTOS.org is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FreeRTOS.org; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

	A special exception to the GPL can be applied should you wish to distribute
	a combined work that includes FreeRTOS.org, without being obliged to provide
	the source code for any proprietary components.  See the licensing section
	of http://www.FreeRTOS.org for full details of how and when the exception
	can be applied.

	***************************************************************************

	Please ensure to read the configuration and relevant port sections of the 
	online documentation.

	+++ http://www.FreeRTOS.org +++
	Documentation, latest information, license and contact details.  

	+++ http://www.SafeRTOS.com +++
	A version that is certified for use in safety critical systems.

	+++ http://www.OpenRTOS.com +++
	Commercial support, development, porting, licensing and training services.

	***************************************************************************
*/


/*
 * Creates all the demo application tasks, then starts the scheduler.  The WEB
 * documentation provides more details of the standard demo application tasks.
 * In addition to the standard demo tasks, the following tasks and tests are
 * defined and/or created within this file:
 *
 * "Fast Interrupt Test" - A high frequency periodic interrupt is generated
 * using a free running timer to demonstrate the use of the
 * configKERNEL_INTERRUPT_PRIORITY configuration constant.  The interrupt
 * service routine measures the number of processor clocks that occur between
 * each interrupt - and in so doing measures the jitter in the interrupt timing.
 * The maximum measured jitter time is latched in the ulMaxJitter variable, and
 * displayed on the OLED display by the 'OLED' task as described below.  The
 * fast interrupt is configured and handled in the timertest.c source file.
 *
 * "OLED" task - the OLED task is a 'gatekeeper' task.  It is the only task that
 * is permitted to access the display directly.  Other tasks wishing to write a
 * message to the OLED send the message on a queue to the OLED task instead of
 * accessing the OLED themselves.  The OLED task just blocks on the queue waiting
 * for messages - waking and displaying the messages as they arrive.
 *
 * "Check" hook -  This only executes every five seconds from the tick hook.
 * Its main function is to check that all the standard demo tasks are still 
 * operational.  Should any unexpected behaviour within a demo task be discovered 
 * the tick hook will write an error to the OLED (via the OLED task).  If all the 
 * demo tasks are executing with their expected behaviour then the check task 
 * writes PASS to the OLED (again via the OLED task), as described above.
 *
 * "uIP" task -  This is the task that handles the uIP stack.  All TCP/IP
 * processing is performed in this task.
 */



/* Standard includes. */
#include <stdio.h>

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* Demo app includes. */
#include "BlockQ.h"
#include "death.h"
#include "integer.h"
#include "blocktim.h"
#include "flash.h"
#include "partest.h"
#include "semtest.h"
#include "PollQ.h"
#include "lcd_message.h"
#include "bitmap.h"
#include "GenQTest.h"
#include "QPeek.h"
#include "recmutex.h"

/* Hardware library includes. */
#include "hw_memmap.h"
#include "hw_types.h"
#include "hw_sysctl.h"
#include "sysctl.h"
#include "gpio.h"
#include "rit128x96x4.h"
#include "osram128x64x4.h"

/*-----------------------------------------------------------*/

/* The time between cycles of the 'check' functionality (defined within the
tick hook. */
#define mainCHECK_DELAY						( ( portTickType ) 5000 / portTICK_RATE_MS )

/* Size of the stack allocated to the uIP task. */
#define mainBASIC_WEB_STACK_SIZE            ( configMINIMAL_STACK_SIZE * 3 )

/* The OLED task uses the sprintf function so requires a little more stack too. */
#define mainOLED_TASK_STACK_SIZE			( configMINIMAL_STACK_SIZE + 50 )

/* Task priorities. */
#define mainQUEUE_POLL_PRIORITY				( tskIDLE_PRIORITY + 2 )
#define mainCHECK_TASK_PRIORITY				( tskIDLE_PRIORITY + 3 )
#define mainSEM_TEST_PRIORITY				( tskIDLE_PRIORITY + 1 )
#define mainBLOCK_Q_PRIORITY				( tskIDLE_PRIORITY + 2 )
#define mainCREATOR_TASK_PRIORITY           ( tskIDLE_PRIORITY + 3 )
#define mainINTEGER_TASK_PRIORITY           ( tskIDLE_PRIORITY )
#define mainGEN_QUEUE_TASK_PRIORITY			( tskIDLE_PRIORITY )

/* The maximum number of message that can be waiting for display at any one
time. */
#define mainOLED_QUEUE_SIZE					( 3 )

/* Dimensions the buffer into which the jitter time is written. */
#define mainMAX_MSG_LEN						25

/* The period of the system clock in nano seconds.  This is used to calculate
the jitter time in nano seconds. */
#define mainNS_PER_CLOCK					( ( unsigned portLONG ) ( ( 1.0 / ( double ) configCPU_CLOCK_HZ ) * 1000000000.0 ) )

/* Constants used when writing strings to the display. */
#define mainCHARACTER_HEIGHT				( 9 )
#define mainMAX_ROWS_96						( mainCHARACTER_HEIGHT * 10 )
#define mainMAX_ROWS_64						( mainCHARACTER_HEIGHT * 7 )
#define mainFULL_SCALE						( 15 )
#define ulSSI_FREQUENCY						( 3500000UL )

/*-----------------------------------------------------------*/

/*
 * The task that handles the uIP stack.  All TCP/IP processing is performed in
 * this task.
 */
extern void vuIP_Task( void *pvParameters );

/*
 * The display is written two by more than one task so is controlled by a
 * 'gatekeeper' task.  This is the only task that is actually permitted to
 * access the display directly.  Other tasks wanting to display a message send
 * the message to the gatekeeper.
 */
static void vOLEDTask( void *pvParameters );

/*
 * Configure the hardware for the demo.
 */
static void prvSetupHardware( void );

/*
 * Configures the high frequency timers - those used to measure the timing
 * jitter while the real time kernel is executing.
 */
extern void vSetupTimer( void );

/*
 * The idle hook is used to run a test of the scheduler context switch
 * mechanism.
 */
void vApplicationIdleHook( void ) __attribute__((naked));
/*-----------------------------------------------------------*/

/* The queue used to send messages to the OLED task. */
xQueueHandle xOLEDQueue;

/* The welcome text. */
const portCHAR * const pcWelcomeMessage = "   www.FreeRTOS.org";

/* Variables used to detect the test in the idle hook failing. */
unsigned portLONG ulIdleError = pdFALSE;

/*-----------------------------------------------------------*/

int main( void )
{
	prvSetupHardware();

	/* Create the queue used by the OLED task.  Messages for display on the OLED
	are received via this queue. */
	xOLEDQueue = xQueueCreate( mainOLED_QUEUE_SIZE, sizeof( xOLEDMessage ) );

	/* Create the uIP task if running on a processor that includes a MAC and 
	PHY. */
	if( SysCtlPeripheralPresent( SYSCTL_PERIPH_ETH ) )
	{
		xTaskCreate( vuIP_Task, ( signed portCHAR * ) "uIP", mainBASIC_WEB_STACK_SIZE, NULL, mainCHECK_TASK_PRIORITY - 1, NULL );
	}

	/* Start the standard demo tasks. */
	vStartBlockingQueueTasks( mainBLOCK_Q_PRIORITY );
    vCreateBlockTimeTasks();
    vStartSemaphoreTasks( mainSEM_TEST_PRIORITY );
    vStartPolledQueueTasks( mainQUEUE_POLL_PRIORITY );
    vStartIntegerMathTasks( mainINTEGER_TASK_PRIORITY );
    vStartGenericQueueTasks( mainGEN_QUEUE_TASK_PRIORITY );
    vStartQueuePeekTasks();
    vStartRecursiveMutexTasks();

	/* Start the tasks defined within this file/specific to this demo. */
	xTaskCreate( vOLEDTask, ( signed portCHAR * ) "OLED", mainOLED_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL );

	/* The suicide tasks must be created last as they need to know how many
	tasks were running prior to their creation in order to ascertain whether
	or not the correct/expected number of tasks are running at any given time. */
    vCreateSuicidalTasks( mainCREATOR_TASK_PRIORITY );

	/* Configure the high frequency interrupt used to measure the interrupt
	jitter time. */
	vSetupTimer();
	
	/* Start the scheduler. */
	vTaskStartScheduler();

    /* Will only get here if there was insufficient memory to create the idle
    task. */
	return 0;
}
/*-----------------------------------------------------------*/

void prvSetupHardware( void )
{
    /* If running on Rev A2 silicon, turn the LDO voltage up to 2.75V.  This is
    a workaround to allow the PLL to operate reliably. */
    if( DEVICE_IS_REVA2 )
    {
        SysCtlLDOSet( SYSCTL_LDO_2_75V );
    }
	
	/* Set the clocking to run from the PLL at 50 MHz */
	SysCtlClockSet( SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN | SYSCTL_XTAL_8MHZ );
	
	/* 	Enable Port F for Ethernet LEDs
		LED0        Bit 3   Output
		LED1        Bit 2   Output */
	SysCtlPeripheralEnable( SYSCTL_PERIPH_GPIOF );
	GPIODirModeSet( GPIO_PORTF_BASE, (GPIO_PIN_2 | GPIO_PIN_3), GPIO_DIR_MODE_HW );
	GPIOPadConfigSet( GPIO_PORTF_BASE, (GPIO_PIN_2 | GPIO_PIN_3 ), GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD );	
	
	vParTestInitialise();
}
/*-----------------------------------------------------------*/

void vApplicationTickHook( void )
{
static xOLEDMessage xMessage = { "PASS" };
static unsigned portLONG ulTicksSinceLastDisplay = 0;

	/* Called from every tick interrupt.  Have enough ticks passed to make it
	time to perform our health status check again? */
	ulTicksSinceLastDisplay++;
	if( ulTicksSinceLastDisplay >= mainCHECK_DELAY )
	{
		ulTicksSinceLastDisplay = 0;
		
		/* Has an error been found in any task? */
		if( xAreGenericQueueTasksStillRunning() != pdTRUE )
		{
			xMessage.pcMessage = "ERROR IN GEN Q";
		}
		else if( xAreQueuePeekTasksStillRunning() != pdTRUE )
		{
			xMessage.pcMessage = "ERROR IN PEEK Q";
		}
		else if( xAreBlockingQueuesStillRunning() != pdTRUE )
		{
			xMessage.pcMessage = "ERROR IN BLOCK Q";
		}
		else if( xAreBlockTimeTestTasksStillRunning() != pdTRUE )
		{
			xMessage.pcMessage = "ERROR IN BLOCK TIME";
		}
	    else if( xAreSemaphoreTasksStillRunning() != pdTRUE )
	    {
	        xMessage.pcMessage = "ERROR IN SEMAPHORE";
	    }
	    else if( xArePollingQueuesStillRunning() != pdTRUE )
	    {
	        xMessage.pcMessage = "ERROR IN POLL Q";
	    }
	    else if( xIsCreateTaskStillRunning() != pdTRUE )
	    {
	        xMessage.pcMessage = "ERROR IN CREATE";
	    }
	    else if( xAreIntegerMathsTaskStillRunning() != pdTRUE )
	    {
	        xMessage.pcMessage = "ERROR IN MATH";
	    }
	    else if( xAreRecursiveMutexTasksStillRunning() != pdTRUE )
	    {
	    	xMessage.pcMessage = "ERROR IN REC MUTEX";
	    }
		else if( ulIdleError != pdFALSE )
		{
			xMessage.pcMessage = "ERROR IN HOOK";
		}

		/* Send the message to the OLED gatekeeper for display. */
		xQueueSendFromISR( xOLEDQueue, &xMessage, pdFALSE );
	}
}
/*-----------------------------------------------------------*/

void vOLEDTask( void *pvParameters )
{
xOLEDMessage xMessage;
unsigned portLONG ulY, ulMaxY;
static portCHAR cMessage[ mainMAX_MSG_LEN ];
extern unsigned portLONG ulMaxJitter;
unsigned portBASE_TYPE uxUnusedStackOnEntry, uxUnusedStackNow;

/* Functions to access the OLED.  The one used depends on the dev kit
being used. */
void ( *vOLEDInit )( unsigned portLONG );
void ( *vOLEDStringDraw )( const portCHAR *, unsigned portLONG, unsigned portLONG, unsigned portCHAR );
void ( *vOLEDImageDraw )( const unsigned portCHAR *, unsigned portLONG, unsigned portLONG, unsigned portLONG, unsigned portLONG );
void ( *vOLEDClear )( void );

	/* Just for demo purposes. */
	uxUnusedStackOnEntry = uxTaskGetStackHighWaterMark( NULL );

	/* Map the OLED access functions to the driver functions that are appropriate
	for the evaluation kit being used. */	
	switch( HWREG( SYSCTL_DID1 ) & SYSCTL_DID1_PRTNO_MASK )
	{
		case SYSCTL_DID1_PRTNO_6965	:	 
		case SYSCTL_DID1_PRTNO_2965	:	vOLEDInit = OSRAM128x64x4Init;
										vOLEDStringDraw = OSRAM128x64x4StringDraw;
										vOLEDImageDraw = OSRAM128x64x4ImageDraw;
										vOLEDClear = OSRAM128x64x4Clear;
										ulMaxY = mainMAX_ROWS_64;
										break;
										
		default						:	vOLEDInit = RIT128x96x4Init;
										vOLEDStringDraw = RIT128x96x4StringDraw;
										vOLEDImageDraw = RIT128x96x4ImageDraw;
										vOLEDClear = RIT128x96x4Clear;
										ulMaxY = mainMAX_ROWS_96;										
										break;
	}

	ulY = ulMaxY;
	
	/* Initialise the OLED and display a startup message. */
	vOLEDInit( ulSSI_FREQUENCY );	
	vOLEDStringDraw( " POWERED BY FreeRTOS", 0, 0, mainFULL_SCALE );
	vOLEDImageDraw( pucImage, 0, mainCHARACTER_HEIGHT + 1, bmpBITMAP_WIDTH, bmpBITMAP_HEIGHT );
	
	uxUnusedStackNow = uxTaskGetStackHighWaterMark( NULL );
	
	for( ;; )
	{
		/* Wait for a message to arrive that requires displaying. */
		xQueueReceive( xOLEDQueue, &xMessage, portMAX_DELAY );
	
		/* Write the message on the next available row. */
		ulY += mainCHARACTER_HEIGHT;
		if( ulY >= ulMaxY )
		{
			ulY = mainCHARACTER_HEIGHT;
			vOLEDClear();
			vOLEDStringDraw( pcWelcomeMessage, 0, 0, mainFULL_SCALE );			
		}

		/* Display the message along with the maximum jitter time from the 
		high priority time test. */
		sprintf( cMessage, "%s [%uns]", xMessage.pcMessage, ulMaxJitter * mainNS_PER_CLOCK );
		vOLEDStringDraw( cMessage, 0, ulY, mainFULL_SCALE );
	}
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
	/* This is just a sanity check function to test the port is
	functioning correctly.  It can be removed from real applications.
	
	Fill the general purpose registers with known values. */
	__asm volatile( "    mov r11, #10		\n"
					"    add r0, r11, #1	\n"
					"    add r1, r11, #2	\n"
		            "    add r2, r11, #3	\n"
		            "    add r3, r11, #4	\n"
		            "    add r4, r11, #5	\n"
		            "    add r5, r11, #6	\n"
		            "    add r6, r11, #7	\n"
		            "    add r7, r11, #8	\n"
		            "    add r8, r11, #9	\n"
		            "    add r9, r11, #10	\n"
		            "    add r10, r11, #11	\n"
		            "    add r12, r11, #12" );
	
	/* Check the values are as expected.  A context switch might
	have occurred since setting the register values. */
	__asm volatile( "    cmp r11, #10			\n"
		            "    bne set_error_flag		\n"
		            "    cmp r0, #11			\n"
		            "    bne set_error_flag		\n"
		            "    cmp r1, #12			\n"
		            "    bne set_error_flag		\n"
		            "    cmp r2, #13			\n"
		            "    bne set_error_flag		\n"
		            "    cmp r3, #14			\n"
		            "    bne set_error_flag		\n"
		            "    cmp r4, #15			\n"
		            "    bne set_error_flag		\n"
		            "    cmp r5, #16			\n"
		            "    bne set_error_flag		\n"
		            "    cmp r6, #17			\n"
		            "    bne set_error_flag		\n"
		            "    cmp r7, #18			\n"
		            "    bne set_error_flag		\n"
		            "    cmp r8, #19			\n"
		            "    bne set_error_flag		\n"
		            "    cmp r9, #20			\n"
		            "    bne set_error_flag		\n"
		            "    cmp r10, #21			\n"
		            "    bne set_error_flag		\n"
		            "    cmp r12, #22			\n"
		            "    bne set_error_flag		\n"
					"	 bx r14 				\n"
					" 							\n"	/* If an error is detected in the */					
					"set_error_flag:			\n" /* value of a register then the error */
					"	ldr r1, ulIdleErrorConst\n" /* variable will be set to true.  This */
					"	mov r0, #1				\n" /* will cause	an error message to be */
					"	str r0, [r1]			\n" /* written to the OLED. */
					"	bx r14 					\n"
					"							\n"
					"	.align 2				\n"			
					"ulIdleErrorConst: .word ulIdleError" );
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( xTaskHandle *pxTask, signed portCHAR *pcTaskName )
{
	for( ;; );
}

