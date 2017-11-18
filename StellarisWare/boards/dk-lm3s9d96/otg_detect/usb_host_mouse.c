//*****************************************************************************
//
// usb_host_mouse.c - main application code for the host mouse example.
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
// This is part of revision 10636 of the DK-LM3S9D96 Firmware Package.
//
//*****************************************************************************

#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "driverlib/sysctl.h"
#include "grlib/grlib.h"
#include "usblib/usblib.h"
#include "usblib/usbhid.h"
#include "usblib/host/usbhost.h"
#include "usblib/host/usbhhid.h"
#include "usblib/host/usbhhidmouse.h"
#ifdef DEBUG
#include "utils/uartstdio.h"
#endif
#include "drivers/kitronix320x240x16_ssd2119_8bit.h"
#include "drivers/touch.h"
#include "otg_detect.h"

//*****************************************************************************
//
// The size of the mouse device interface's memory pool in bytes.
//
//*****************************************************************************
#define MOUSE_MEMORY_SIZE       128

//*****************************************************************************
//
// The memory pool to provide to the mouse device.
//
//*****************************************************************************
unsigned char g_pucBuffer[MOUSE_MEMORY_SIZE];

//*****************************************************************************
//
// Declare the USB Events driver interface.
//
//*****************************************************************************
DECLARE_EVENT_DRIVER(g_sUSBEventDriver, 0, 0, USBHCDEvents);

//*****************************************************************************
//
// The global that holds all of the host drivers in use in the application.
// In this case, only the Mouse class is loaded.
//
//*****************************************************************************
static tUSBHostClassDriver const * const g_ppHostClassDrivers[] =
{
    &g_USBHIDClassDriver
    ,&g_sUSBEventDriver
};

//*****************************************************************************
//
// This global holds the number of class drivers in the g_ppHostClassDrivers
// list.
//
//*****************************************************************************
static const unsigned long g_ulNumHostClassDrivers =
    sizeof(g_ppHostClassDrivers) / sizeof(tUSBHostClassDriver *);

//*****************************************************************************
//
// The global value used to store the mouse instance value.
//
//*****************************************************************************
static unsigned long g_ulMouseInstance;

//*****************************************************************************
//
// The global values used to store the mouse state.
//
//*****************************************************************************
static unsigned long g_ulButtons;
static tRectangle g_sCursor;

//*****************************************************************************
//
// This enumerated type is used to hold the states of the mouse.
//
//*****************************************************************************
enum
{
    //
    // No device is present.
    //
    STATE_NO_DEVICE,

    //
    // Mouse has been detected and needs to be initialized in the main
    // loop.
    //
    STATE_MOUSE_INIT,

    //
    // Mouse is connected and waiting for events.
    //
    STATE_MOUSE_CONNECTED,

    //
    // An unsupported device has been attached.
    //
    STATE_UNKNOWN_DEVICE,

    //
    // A power fault has occurred.
    //
    STATE_POWER_FAULT
}
eUSBState;

//*****************************************************************************
//
// These defines are used to define the screen constraints to the application.
//
//*****************************************************************************
#define DISPLAY_BANNER_HEIGHT   14
#define DISPLAY_BANNER_BG       ClrDarkBlue
#define DISPLAY_BANNER_FG       ClrWhite
#define DISPLAY_MOUSE_BG        ClrBlack
#define DISPLAY_MOUSE_FG        ClrWhite
#define DISPLAY_MOUSE_SIZE      2

//*****************************************************************************
//
// This function clears the main application screen area.
//
//*****************************************************************************
void
ClearMainWindow(void)
{
    tRectangle sRect;

    //
    // Initialize the button indicator.
    //
    sRect.sXMin = 0;
    sRect.sYMin = DISPLAY_BANNER_HEIGHT + 1;
    sRect.sXMax = GrContextDpyWidthGet(&g_sContext) - 1;
    sRect.sYMax = GrContextDpyHeightGet(&g_sContext) - DISPLAY_BANNER_HEIGHT;

    GrContextForegroundSet(&g_sContext, DISPLAY_MOUSE_BG);
    GrRectFill(&g_sContext, &sRect);
    GrContextForegroundSet(&g_sContext, DISPLAY_MOUSE_FG);
}

//*****************************************************************************
//
// This function updates the cursor position based on deltas received from
// the mouse device.
//
// \param iXDelta is the signed movement in the X direction.
// \param iYDelta is the signed movement in the Y direction.
//
// This function is called by the mouse handler code when it detects a change
// in the position of the mouse.  It will take the inputs and force them
// to be constrained to the display area of the screen.  If the left mouse
// button is pressed then the mouse will draw on the screen and if it is not
// it will move around normally.  A side effect of not being able to read the
// current state of the screen is that the cursor will erase anything it moves
// over while the left mouse button is not pressed.
//
// \return None.
//
//*****************************************************************************
void
UpdateCursor(int iXDelta, int iYDelta)
{
    int iTemp;

    //
    // If the left button is not pressed then erase the previous cursor
    // position.
    //
    if((g_ulButtons & 1) == 0)
    {
        //
        // Erase the previous cursor.
        //
        GrContextForegroundSet(&g_sContext, DISPLAY_MOUSE_BG);
        GrRectFill(&g_sContext, &g_sCursor);
    }

    //
    // Need to do signed math so use the temporary signed value.
    //
    iTemp = g_sCursor.sXMin;

    //
    // Update the X position without going off the screen.
    //
    if(((int)g_sCursor.sXMin + iXDelta + DISPLAY_MOUSE_SIZE) <
       GrContextDpyWidthGet(&g_sContext))
    {
        //
        // Update the X cursor position.
        //
        iTemp += iXDelta;

        //
        // Don't let the cursor go off the left of the screen either.
        //
        if(iTemp < 0)
        {
            iTemp = 0;
        }
    }

    //
    // Update the X position.
    //
    g_sCursor.sXMin = iTemp;
    g_sCursor.sXMax = iTemp + DISPLAY_MOUSE_SIZE;

    //
    // Need to do signed math so use the temporary signed value.
    //
    iTemp = g_sCursor.sYMin;

    //
    // Update the Y position without going off the screen.
    //
    if(((int)g_sCursor.sYMin + iYDelta) < (GrContextDpyHeightGet(&g_sContext) -
       DISPLAY_BANNER_HEIGHT - DISPLAY_MOUSE_SIZE - 1))
    {
        //
        // Update the Y cursor position.
        //
        iTemp += iYDelta;

        //
        // Don't let the cursor overwrite the status area of the screen.
        //
        if(iTemp < DISPLAY_BANNER_HEIGHT + 1)
        {
            iTemp = DISPLAY_BANNER_HEIGHT + 1;
        }
    }

    //
    // Update the Y position.
    //
    g_sCursor.sYMin = iTemp;
    g_sCursor.sYMax = iTemp + DISPLAY_MOUSE_SIZE;

    //
    // Draw the new cursor.
    //
    GrContextForegroundSet(&g_sContext, DISPLAY_MOUSE_FG);
    GrRectFill(&g_sContext, &g_sCursor);
}

//*****************************************************************************
//
// This function will update the small mouse button indicators in the status
// bar area of the screen.  This can be called on its own or it will be called
// whenever UpdateStatus() is called as well.
//
//*****************************************************************************
void
UpdateButtons(void)
{
    tRectangle sRect, sRectInner;
    int iButton;

    //
    // Initialize the button indicator position.
    //
    sRect.sXMin = GrContextDpyWidthGet(&g_sContext) - 30;
    sRect.sYMin = GrContextDpyHeightGet(&g_sContext) - 12;
    sRect.sXMax = sRect.sXMin + 6;
    sRect.sYMax = sRect.sYMin + 8;
    sRectInner.sXMin = sRect.sXMin + 1;
    sRectInner.sYMin = sRect.sYMin + 1;
    sRectInner.sXMax = sRect.sXMax - 1;
    sRectInner.sYMax = sRect.sYMax - 1;

    //
    // Check all three buttons.
    //
    for(iButton = 0; iButton < 3; iButton++)
    {
        //
        // Draw the button indicator red if pressed and black if not pressed.
        //
        if(g_ulButtons & (1 << iButton))
        {
            GrContextForegroundSet(&g_sContext, ClrRed);
        }
        else
        {
            GrContextForegroundSet(&g_sContext, ClrBlack);
        }

        //
        // Draw the back of the  button indicator.
        //
        GrRectFill(&g_sContext, &sRectInner);

        //
        // Draw the border on the button indicator.
        //
        GrContextForegroundSet(&g_sContext, ClrWhite);
        GrRectDraw(&g_sContext, &sRect);

        //
        // Move to the next button indicator position.
        //
        sRect.sXMin += 8;
        sRect.sXMax += 8;
        sRectInner.sXMin += 8;
        sRectInner.sXMax += 8;
    }
}

//*****************************************************************************
//
// This function updates the status area of the screen.  It uses the current
// state of the application to print the status bar.
//
//*****************************************************************************
void
UpdateStatus(char *pcString, unsigned long ulButtons, tBoolean bClrBackground)
{
    tRectangle sRect;

    //
    // Fill the bottom rows of the screen with blue to create the status area.
    //
    sRect.sXMin = 0;
    sRect.sYMin = GrContextDpyHeightGet(&g_sContext) -
                  DISPLAY_BANNER_HEIGHT - 1;
    sRect.sXMax = GrContextDpyWidthGet(&g_sContext) - 1;
    sRect.sYMax = sRect.sYMin + DISPLAY_BANNER_HEIGHT;

    //
    // Were we asked to clear the background of the status area?
    //
    GrContextBackgroundSet(&g_sContext, DISPLAY_BANNER_BG);

    if(bClrBackground)
    {
        //
        // Draw the background of the banner.
        //
        GrContextForegroundSet(&g_sContext, DISPLAY_BANNER_BG);
        GrRectFill(&g_sContext, &sRect);

        //
        // Put a white box around the banner.
        //
        GrContextForegroundSet(&g_sContext, DISPLAY_BANNER_FG);
        GrRectDraw(&g_sContext, &sRect);
    }

    //
    // Write the current state to the left of the status area.
    //
    GrContextFontSet(&g_sContext, g_pFontFixed6x8);

    //
    // Update the status on the screen.
    //
    if(pcString != 0)
    {
        GrStringDraw(&g_sContext, pcString, -1, 4, sRect.sYMin + 4, 1);

        g_ulButtons = ulButtons;
    }
    else if(eUSBState == STATE_NO_DEVICE)
    {
        //
        // Mouse is currently disconnected.
        //
        GrStringDraw(&g_sContext, "no device     ", -1, 4, sRect.sYMin + 4, 1);
    }
    else if(eUSBState == STATE_MOUSE_CONNECTED)
    {
        //
        // Mouse is connected.
        //
        GrStringDraw(&g_sContext, "connected     ", -1, 4, sRect.sYMin + 4, 1);
    }
    else if(eUSBState == STATE_UNKNOWN_DEVICE)
    {
        //
        // Some other (unknown) device is connected.
        //
        GrStringDraw(&g_sContext, "unknown device", -1, 4, sRect.sYMin + 4, 1);
    }
    else if(eUSBState == STATE_POWER_FAULT)
    {
        //
        // Power fault.
        //
        GrStringDraw(&g_sContext, "power fault   ", -1, 4, sRect.sYMin + 4, 1);
    }

    UpdateButtons();
}

//*****************************************************************************
//
// This is the generic callback from host stack.
//
// \param pvData is actually a pointer to a tEventInfo structure.
//
// This function will be called to inform the application when a USB event has
// occurred that is outside those related to the mouse device.  At this
// point this is used to detect unsupported devices being inserted and removed.
// It is also used to inform the application when a power fault has occurred.
// This function is required when the g_USBGenericEventDriver is included in
// the host controller driver array that is passed in to the
// USBHCDRegisterDrivers() function.
//
// \return None.
//
//*****************************************************************************
void
USBHCDEvents(void *pvData)
{
    tEventInfo *pEventInfo;

    //
    // Cast this pointer to its actual type.
    //
    pEventInfo = (tEventInfo *)pvData;

    switch(pEventInfo->ulEvent)
    {
        //
        // New mouse detected.
        //
        case USB_EVENT_CONNECTED:
        {
            //
            // See if this is a HID Mouse.
            //
            if((USBHCDDevClass(pEventInfo->ulInstance, 0) == USB_CLASS_HID) &&
               (USBHCDDevProtocol(pEventInfo->ulInstance, 0) ==
                USB_HID_PROTOCOL_MOUSE))
            {
                //
                // Indicate that the mouse has been detected.
                //
                DEBUG_PRINT("Mouse Connected\n");

                //
                // Proceed to the STATE_MOUSE_INIT state so that the main loop
                // can finish initialized the mouse since USBHMouseInit()
                // cannot be called from within a callback.
                //
                eUSBState = STATE_MOUSE_INIT;
            }

            break;
        }
        //
        // Unsupported detected.
        //
        case USB_EVENT_UNKNOWN_CONNECTED:
        {
            DEBUG_PRINT("Unsupported Device Connected\n");

            //
            // An unknown device was detected.
            //
            eUSBState = STATE_UNKNOWN_DEVICE;

            UpdateStatus(0, 0, false);

            break;
        }
        //
        // Device has been unplugged.
        //
        case USB_EVENT_DISCONNECTED:
        {
            //
            // Indicate that the mouse has been disconnected.
            //
            DEBUG_PRINT("Device Disconnected\n");

            //
            // Change the state so that the main loop knows that the mouse is
            // no longer present.
            //
            eUSBState = STATE_NO_DEVICE;

            //
            // Reset the button state.
            //
            g_ulButtons = 0;

            UpdateStatus(0, 0, false);

            break;
        }
        //
        // Power Fault occurred.
        //
        case USB_EVENT_POWER_FAULT:
        {
            DEBUG_PRINT("Power Fault\n");

            //
            // No power means no device is present.
            //
            eUSBState = STATE_POWER_FAULT;

            UpdateStatus(0, 0, false);
            break;
        }
        default:
        {
            break;
        }
    }
}

//*****************************************************************************
//
// This is the callback from the USB HID mouse handler.
//
// \param pvCBData is ignored by this function.
// \param ulEvent is one of the valid events for a mouse device.
// \param ulMsgParam is defined by the event that occurs.
// \param pvMsgData is a pointer to data that is defined by the event that
// occurs.
//
// This function will be called to inform the application when a mouse has
// been plugged in or removed and any time mouse movement or button pressed
// is detected.
//
// \return This function will return 0.
//
//*****************************************************************************
unsigned long
MouseCallback(void *pvCBData, unsigned long ulEvent, unsigned long ulMsgParam,
              void *pvMsgData)
{
    switch(ulEvent)
    {
        //
        // Mouse button press detected.
        //
        case USBH_EVENT_HID_MS_PRESS:
        {
            DEBUG_PRINT("Button Pressed %02x\n", ulMsgParam);

            //
            // Save the new button that was pressed.
            //
            g_ulButtons |= ulMsgParam;

            break;
        }

        //
        // Mouse button release detected.
        //
        case USBH_EVENT_HID_MS_REL:
        {
            DEBUG_PRINT("Button Released %02x\n", ulMsgParam);

            //
            // Remove the button from the pressed state.
            //
            g_ulButtons &= ~ulMsgParam;

            break;
        }

        //
        // Mouse X movement detected.
        //
        case USBH_EVENT_HID_MS_X:
        {
            DEBUG_PRINT("X:%02d.\n", (signed char)ulMsgParam);

            //
            // Update the cursor on the screen.
            //
            UpdateCursor((signed char)ulMsgParam, 0);

            break;
        }

        //
        // Mouse Y movement detected.
        //
        case USBH_EVENT_HID_MS_Y:
        {
            DEBUG_PRINT("Y:%02d.\n", (signed char)ulMsgParam);

            //
            // Update the cursor on the screen.
            //
            UpdateCursor(0, (signed char)ulMsgParam);

            break;
        }
    }

    //
    // Update the status area of the screen.
    //
    UpdateStatus(0, 0, false);

    return(0);
}

//*****************************************************************************
//
// Initialize the host mode stack.
//
//*****************************************************************************
void
HostInit(void)
{
    //
    // Register the host class drivers.
    //
    USBHCDRegisterDrivers(0, g_ppHostClassDrivers, g_ulNumHostClassDrivers);

    //
    // Initialize the button states.
    //
    g_ulButtons = 0;

    //
    // Update the status on the screen.
    //
    UpdateStatus(0, 0, true);

    //
    // Open an instance of the mouse driver.  The mouse does not need
    // to be present at this time, this just saves a place for it and allows
    // the applications to be notified when a mouse is present.
    //
    g_ulMouseInstance =
        USBHMouseOpen(MouseCallback, g_pucBuffer, MOUSE_MEMORY_SIZE);

    //
    // Configure the power pins for host mode.
    //
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    GPIOPinTypeUSBDigital(GPIO_PORTA_BASE, GPIO_PIN_6 | GPIO_PIN_7);

    //
    // Initialize the power configuration. This sets the power enable signal
    // to be active high and does not enable the power fault.
    //
    USBHCDPowerConfigInit(0, USBHCD_VBUS_AUTO_HIGH | USBHCD_VBUS_FILTER);

    //
    // Call the main loop for the Host controller driver.
    //
    eUSBState = STATE_NO_DEVICE;
}

//*****************************************************************************
//
// This is the main loop that runs the application.
//
//*****************************************************************************
void
HostMain(void)
{
    switch(eUSBState)
    {
        //
        // This state is entered when the mouse is first detected.
        //
        case STATE_MOUSE_INIT:
        {
            //
            // Initialize the newly connected mouse.
            //
            USBHMouseInit(g_ulMouseInstance);

            //
            // Proceed to the mouse connected state.
            //
            eUSBState = STATE_MOUSE_CONNECTED;

            //
            // Update the status on the screen.
            //
            UpdateStatus(0, 0, true);

            //
            // Update the cursor on the screen.
            //
            UpdateCursor(GrContextDpyWidthGet(&g_sContext) / 2,
                         GrContextDpyHeightGet(&g_sContext)/ 2);

            break;
        }
        case STATE_MOUSE_CONNECTED:
        {
            //
            // Nothing is currently done in the main loop when the mouse
            // is connected.
            //
            break;
        }
        case STATE_NO_DEVICE:
        {
            //
            // The mouse is not connected so nothing needs to be done here.
            //
            break;
        }
        default:
        {
            break;
        }
    }

    //
    // Periodically call the main loop for the Host controller driver.
    //
    USBHCDMain();
}
