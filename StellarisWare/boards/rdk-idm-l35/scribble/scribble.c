//*****************************************************************************
//
// scribble.c - A simple scribble pad to demonstrate the touch screen driver.
//
// Copyright (c) 2008-2013 Texas Instruments Incorporated.  All rights reserved.
// Software License Agreement
// 
// Texas Instruments (TI) is supplying this software for use solely and
// exclusively on TI's microcontroller products. The software is owned by
// TI and/or its suppliers, and is protected under applicable copyright
// laws. You may not combine this software with "viral" open-source
// software in order to form a larger program.
// 
// THIS SOFTWARE IS PROVIDED "AS IS" AND WITH ALL FAULTS.
// NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT
// NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. TI SHALL NOT, UNDER ANY
// CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR CONSEQUENTIAL
// DAMAGES, FOR ANY REASON WHATSOEVER.
// 
// This is part of revision 10636 of the RDK-IDM-L35 Firmware Package.
//
//*****************************************************************************

#include "inc/hw_memmap.h"
#include "inc/hw_nvic.h"
#include "inc/hw_sysctl.h"
#include "inc/hw_types.h"
#include "driverlib/flash.h"
#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "driverlib/sysctl.h"
#include "driverlib/systick.h"
#include "driverlib/uart.h"
#include "grlib/grlib.h"
#include "grlib/widget.h"
#include "utils/cmdline.h"
#include "utils/ringbuf.h"
#include "utils/uartstdio.h"
#include "utils/ustdlib.h"
#include "drivers/kitronix320x240x16_ssd2119.h"
#include "drivers/touch.h"

//*****************************************************************************
//
//! \addtogroup example_list
//! <h1>Scribble Pad (scribble)</h1>
//!
//! The scribble pad provides a drawing area on the screen.  Touching the
//! screen will draw onto the drawing area using a selection of fundamental
//! colors (in other words, the seven colors produced by the three color
//! channels being either fully on or fully off).  Each time the screen is
//! touched to start a new drawing, the drawing area is erased and the next
//! color is selected.  This behavior can be modified using various commands
//! entered via a terminal emulator connected to the IDM-L35 UART.
//!
//! UART0, which is connected to the 3 pin header on the underside of the
//! IDM-L35 RDK board (J3), is configured for 115,200 bits per second, and
//! 8-n-1 mode.  When the program is started a message will be printed to the
//! terminal.  Type ``help'' for command help.
//!
//! This application supports remote software update over serial using the
//! LM Flash Programmer application.  Firmware updates can be initiated by
//! entering the "swupd" command on the serial terminal.  The LMFlash serial
//! data rate must be set to 115200bps and the "Program Address Offset" to
//! 0x800.
//!
//! UART0, which is connected to the 6 pin header on the underside of the
//! IDM-L35 RDK board (J8), is configured for 115200bps, and 8-n-1 mode.  The
//! USB-to-serial cable supplied with the IDM-L35 RDK may be used to connect
//! this TTL-level UART to the host PC to allow firmware update.
//
//*****************************************************************************

//*****************************************************************************
//
// Defines the size of the buffer that holds the command line.
//
//*****************************************************************************
#define CMD_BUF_SIZE    64

//*****************************************************************************
//
// The buffer that holds the command line.
//
//*****************************************************************************
static char g_cCmdBuf[CMD_BUF_SIZE];

//*****************************************************************************
//
// A structure used to pass touchscreen messages from the interrupt-context
// handler function to the main loop for processing.
//
//*****************************************************************************
typedef struct
{
    unsigned long ulMsg;
    long lX;
    long lY;
}
tScribbleMessage;

//*****************************************************************************
//
// The number of messages we can store in the message queue.
//
//*****************************************************************************
#define MSG_QUEUE_SIZE 16

//*****************************************************************************
//
// The ring buffer memory and control structure we use to implement the
// message queue.
//
//*****************************************************************************
static tScribbleMessage g_psMsgQueueBuffer[MSG_QUEUE_SIZE];
static tRingBufObject g_sMsgQueue;

//*****************************************************************************
//
// The error routine that is called if the driver library encounters an error.
//
//*****************************************************************************
#ifdef DEBUG
void
__error__(char *pcFilename, unsigned long ulLine)
{
}
#endif

//*****************************************************************************
//
// The colors that are used to draw on the screen.
//
//*****************************************************************************
static const unsigned long g_pulColors[] =
{
    ClrWhite,
    ClrYellow,
    ClrMagenta,
    ClrRed,
    ClrCyan,
    ClrLime,
    ClrBlue
};

//*****************************************************************************
//
// The index to the current color in use.
//
//*****************************************************************************
static unsigned long g_ulColorIdx;

//*****************************************************************************
//
// The previous pen position returned from the touch screen driver.
//
//*****************************************************************************
static long g_lX, g_lY;

//*****************************************************************************
//
// The drawing context used to draw to the screen.
//
//*****************************************************************************
static tContext g_sContext;

//*****************************************************************************
//
// A flag which indicates whether or not the display should be cleared whenever
// a new press is detected.
//
//*****************************************************************************
static tBoolean g_bClearScreenOnTouch = true;

//*****************************************************************************
//
// Transfer control to the bootloader to wait for an ethernet-based firmware
// update to occur.
//
//*****************************************************************************
void
UpdateFirmware(void)
{
    //
    // Tell the user what's up
    //
    GrContextForegroundSet(&g_sContext, ClrWhite);
    GrStringDrawCentered(&g_sContext, "Updating firmware...", -1,
                         GrContextDpyWidthGet(&g_sContext) / 2,
                         GrContextDpyHeightGet(&g_sContext) / 2,
                         true);

    //
    // Disable all processor interrupts.  Instead of disabling them
    // one at a time (and possibly missing an interrupt if new sources
    // are added), a direct write to NVIC is done to disable all
    // peripheral interrupts.
    //
    IntMasterDisable();
    SysTickIntDisable();
    HWREG(NVIC_DIS0) = 0xffffffff;
    HWREG(NVIC_DIS1) = 0xffffffff;

    //
    // Return control to the boot loader.  This is a call to the SVC
    // handler in the boot loader.
    //
    (*((void (*)(void))(*(unsigned long *)0x2c)))();
}

//*****************************************************************************
//
// This function implements the "swupd" command.  It transfers control to the
// bootloader to update the firmware via ethernet.
//
//*****************************************************************************
int
Cmd_update(int argc, char *argv[])
{
    //
    // Tell the user what we are doing.
    //
    UARTprintf("Serial firmware update requested.\n");

    //
    // Transfer control to the bootloader.
    //
    UARTprintf("Transfering control to boot loader...\n\n");
    UARTprintf("***********************************\n");
    UARTprintf("*** Close your serial terminal ****\n");
    UARTprintf("***   before running LMFlash.  ****\n");
    UARTprintf("***********************************\n\n");
    UARTFlushTx(false);
    UpdateFirmware();

    //
    // The boot loader should take control, so this should never be reached.
    // Just in case, loop forever.
    //
    while(1)
    {
    }
}

//*****************************************************************************
//
// This function implements the "autocls" command.  It sets a global flag
// indicating whether the screen should be cleared on not each time a PTR_DOWN
// event is received.
//
//*****************************************************************************
int
Cmd_AutoCls(int argc, char *argv[])
{
    if(argc != 2)
    {
        UARTprintf("This command requires 1 argument, 0 or 1.\n");
        return(-3);
    }

    //
    // Set whether or not we clear the display on each new touch.
    //
    g_bClearScreenOnTouch = (argv[1][0] == '1') ? true : false;

    UARTprintf("Screen will %s cleared on each new touch.\n",
               g_bClearScreenOnTouch ? "be" : "not be");

    return(0);
}

//*****************************************************************************
//
// This function implements the "help" command.  It displays all the supported
// commands and provides a brief description of each.
//
//*****************************************************************************
int Cmd_help(int argc, char *argv[])
{
    tCmdLineEntry *pEntry;

    //
    // Print some header text.
    //
    UARTprintf("\nAvailable commands\n");
    UARTprintf("------------------\n");

    //
    // Point at the beginning of the command table.
    //
    pEntry = &g_sCmdTable[0];

    //
    // Enter a loop to read each entry from the command table.  The
    // end of the table has been reached when the command name is NULL.
    //
    while(pEntry->pcCmd)
    {
        //
        // Print the command name and the brief description.
        //
        UARTprintf("%s%s\n", pEntry->pcCmd, pEntry->pcHelp);

        //
        // Advance to the next entry in the table.
        //
        pEntry++;
    }

    //
    // Return success.
    //
    return(0);
}

//*****************************************************************************
//
// Clear the drawing area of the display.
//
//*****************************************************************************
int Cmd_Cls(int argc, char *argv[])
{
    tRectangle sRect;

    //
    // Fill the drawing rectangle with black.
    //
    GrContextForegroundSet(&g_sContext, ClrBlack);
    sRect.sXMin = 1;
    sRect.sYMin = 45;
    sRect.sXMax = GrContextDpyWidthGet(&g_sContext) - 2;
    sRect.sYMax = GrContextDpyHeightGet(&g_sContext) - 2;
    GrRectFill(&g_sContext, &sRect);

    //
    // Flush any cached drawing operations.
    //
    GrFlush(&g_sContext);

    //
    // Set the drawing color to the current pen color.
    //
    GrContextForegroundSet(&g_sContext, g_pulColors[g_ulColorIdx]);

    //
    // All is well.
    //
    return(0);
}

//*****************************************************************************
//
// This is the table that holds the command names, implementing functions,
// and brief description.
//
//*****************************************************************************
tCmdLineEntry g_sCmdTable[] =
{
    { "help",   Cmd_help,       " : Display list of commands" },
    { "h",      Cmd_help,    "    : alias for help" },
    { "?",      Cmd_help,    "    : alias for help" },
    { "autocls",Cmd_AutoCls,     ": Clear on each press (1) or not (0)" },
    { "cls",    Cmd_Cls,     "    : Clear the display" },
    { "swupd",  Cmd_update,     " : Initiate a firmware update via serial" },
    { 0, 0, 0 }
};

//*****************************************************************************
//
// The interrupt-context handler for touch screen events from the touch screen
// driver.  This function merely bundles up the event parameters and posts
// them to a message queue.  In the context of the main loop, they will be
// read from the queue and handled using TSMainHandler().
//
//*****************************************************************************
long TSHandler(unsigned long ulMessage, long lX, long lY)
{
    tScribbleMessage sMsg;

    //
    // Build the message that we will write to the queue.
    //
    sMsg.ulMsg = ulMessage;
    sMsg.lX = lX;
    sMsg.lY = lY;

    //
    // Make sure the queue isn't full. If it is, we just ignore this message.
    //
    if(!RingBufFull(&g_sMsgQueue))
    {
        RingBufWrite(&g_sMsgQueue, (unsigned char *)&sMsg,
                     sizeof(tScribbleMessage));
    }

    //
    // Tell the touch handler that everything is fine.
    //
    return(1);
}

//*****************************************************************************
//
// The main loop handler for touch screen events from the touch screen driver.
//
//*****************************************************************************
long
TSMainHandler(unsigned long ulMessage, long lX, long lY)
{
    tRectangle sRect;

    //
    // See which event is being sent from the touch screen driver.
    //
    switch(ulMessage)
    {
        //
        // The pen has just been placed down.
        //
        case WIDGET_MSG_PTR_DOWN:
        {
            //
            // Erase the drawing area if configured to do this.
            //
            if(g_bClearScreenOnTouch)
            {
                GrContextForegroundSet(&g_sContext, ClrBlack);
                sRect.sXMin = 1;
                sRect.sYMin = 45;
                sRect.sXMax = GrContextDpyWidthGet(&g_sContext) - 2;
                sRect.sYMax = GrContextDpyHeightGet(&g_sContext) - 2;
                GrRectFill(&g_sContext, &sRect);

                //
                // Flush any cached drawing operations.
                //
                GrFlush(&g_sContext);
            }

            //
            // Set the drawing color to the current pen color.
            //
            GrContextForegroundSet(&g_sContext, g_pulColors[g_ulColorIdx]);

            //
            // Save the current position.
            //
            g_lX = lX;
            g_lY = lY;

            //
            // This event has been handled.
            //
            break;
        }

        //
        // Then pen has moved.
        //
        case WIDGET_MSG_PTR_MOVE:
        {
            //
            // Draw a line from the previous position to the current position.
            //
            GrLineDraw(&g_sContext, g_lX, g_lY, lX, lY);

            //
            // Flush any cached drawing operations.
            //
            GrFlush(&g_sContext);

            //
            // Save the current position.
            //
            g_lX = lX;
            g_lY = lY;

            //
            // This event has been handled.
            //
            break;
        }

        //
        // The pen has just been picked up.
        //
        case WIDGET_MSG_PTR_UP:
        {
            //
            // Draw a line from the previous position to the current position.
            //
            GrLineDraw(&g_sContext, g_lX, g_lY, lX, lY);

            //
            // Flush any cached drawing operations.
            //
            GrFlush(&g_sContext);

            //
            // Increment to the next drawing color.
            //
            g_ulColorIdx++;
            if(g_ulColorIdx == (sizeof(g_pulColors) / sizeof(g_pulColors[0])))
            {
                g_ulColorIdx = 0;
            }

            //
            // This event has been handled.
            //
            break;
        }
    }

    //
    // Success.
    //
    return(1);
}

//*****************************************************************************
//
// This function is called in the context of the main loop to process any
// touch screen messages that have been sent.  Messages are posted to a
// queue from the message handler and pulled off here.  This is required
// since it is not safe to have two different execution contexts performing
// graphics operations using the same graphics context.
//
//*****************************************************************************
void
ProcessTouchMessages(void)
{
    tScribbleMessage sMsg;

    while(!RingBufEmpty(&g_sMsgQueue))
    {
        //
        // Get the next message.
        //
        RingBufRead(&g_sMsgQueue, (unsigned char *)&sMsg,
                    sizeof(tScribbleMessage));

        //
        // Dispatch it to the handler.
        //
        TSMainHandler(sMsg.ulMsg, sMsg.lX, sMsg.lY);
    }
}

//*****************************************************************************
//
// Provides a scribble pad using the display on the Intelligent Display Module.
//
//*****************************************************************************
int
main(void)
{
    tRectangle sRect;
    int nStatus;

    //
    // If running on Rev A2 silicon, turn the LDO voltage up to 2.75V.  This is
    // a workaround to allow the PLL to operate reliably.
    //
    if(REVISION_IS_A2)
    {
        SysCtlLDOSet(SYSCTL_LDO_2_75V);
    }

    //
    // Set the clocking to run from the PLL.
    //
    SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN |
                   SYSCTL_XTAL_8MHZ);

    //
    // Set GPIO A0 and A1 as UART.
    //
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    //
    // Initialize the UART as a console for text I/O.
    //
    UARTStdioInit(0);

    //
    // Print hello message to user via the serial port.
    //
    UARTprintf("\n\nScribble Example Program\n");
    UARTprintf("Type \'help\' for help.\n");

    //
    // Initialize the display driver.
    //
    Kitronix320x240x16_SSD2119Init();

    //
    // Turn on the backlight.
    //
    Kitronix320x240x16_SSD2119BacklightOn(255);

    //
    // Initialize the graphics context.
    //
    GrContextInit(&g_sContext, &g_sKitronix320x240x16_SSD2119);

    //
    // Fill the top 24 rows of the screen with blue to create the banner.
    //
    sRect.sXMin = 0;
    sRect.sYMin = 0;
    sRect.sXMax = GrContextDpyWidthGet(&g_sContext) - 1;
    sRect.sYMax = 23;
    GrContextForegroundSet(&g_sContext, ClrDarkBlue);
    GrRectFill(&g_sContext, &sRect);

    //
    // Put a white box around the banner.
    //
    GrContextForegroundSet(&g_sContext, ClrWhite);
    GrRectDraw(&g_sContext, &sRect);

    //
    // Put the application name in the middle of the banner.
    //
    GrContextFontSet(&g_sContext, g_pFontCm20);
    GrStringDrawCentered(&g_sContext, "scribble", -1,
                         GrContextDpyWidthGet(&g_sContext) / 2, 11, 0);

    //
    // Print the instructions across the top of the screen in white with a 20
    // point san-serif font.
    //
    GrContextForegroundSet(&g_sContext, ClrWhite);
    GrContextFontSet(&g_sContext, g_pFontCmss20);
    GrStringDrawCentered(&g_sContext, "Touch the screen to draw", -1,
                         GrContextDpyWidthGet(&g_sContext) / 2, 34, 0);

    //
    // Draw a green box around the scribble area.
    //
    sRect.sXMin = 0;
    sRect.sYMin = 44;
    sRect.sXMax = GrContextDpyWidthGet(&g_sContext) - 1;
    sRect.sYMax = GrContextDpyHeightGet(&g_sContext) - 1;
    GrContextForegroundSet(&g_sContext, ClrGreen);
    GrRectDraw(&g_sContext, &sRect);

    //
    // Flush any cached drawing operations.
    //
    GrFlush(&g_sContext);

    //
    // Set the clipping region so that drawing can not occur outside the green
    // box.
    //
    sRect.sXMin++;
    sRect.sYMin++;
    sRect.sXMax--;
    sRect.sYMax--;
    GrContextClipRegionSet(&g_sContext, &sRect);

    //
    // Set the color index to zero.
    //
    g_ulColorIdx = 0;

    //
    // Initialize the message queue we use to pass messages from the touch
    // interrupt handler context to the main loop for processing.
    //
    RingBufInit(&g_sMsgQueue, (unsigned char *)g_psMsgQueueBuffer,
                (MSG_QUEUE_SIZE * sizeof(tScribbleMessage)));

    //
    // Initialize the touch screen driver.
    //
    TouchScreenInit();

    //
    // Set the touch screen event handler.
    //
    TouchScreenCallbackSet(TSHandler);

    //
    // Print a prompt to the console.  Show the CWD.
    //
    UARTprintf("\n> ");

    //
    // Loop forever, checking for touchscreen messages and input via the
    // serial port.
    //
    while(1)
    {
        //
        // Check to see if there are any touchscreen messages to process and
        // handle any that are in the queue.
        //
        ProcessTouchMessages();

        //
        // Check to see if there is a new serial command to process.
        //
        if(UARTPeek('\r') >= 0)
        {
            //
            // A new command has been entered so read it.
            //
            UARTgets(g_cCmdBuf, sizeof(g_cCmdBuf));

            //
            // Pass the line from the user to the command processor.
            // It will be parsed and valid commands executed.
            //
            nStatus = CmdLineProcess(g_cCmdBuf);

            //
            // Handle the case of bad command.
            //
            if(nStatus == CMDLINE_BAD_CMD)
            {
                UARTprintf("Bad command!\n");
            }

            //
            // Handle the case of too many arguments.
            //
            else if(nStatus == CMDLINE_TOO_MANY_ARGS)
            {
                UARTprintf("Too many arguments for command processor!\n");
            }

            //
            // Otherwise the command was executed.  Print the error
            // code if one was returned.
            //
            else if(nStatus != 0)
            {
                UARTprintf("Command returned error code %d\n", nStatus);
            }

            //
            // Print a prompt on the console.
            //
            UARTprintf("\n> ");
        }
    }
}
