// SPDX-License-Identifier: GPL-2.0+
/*
 * hid.c -- HID Composite driver
 *
 * Based on multi.c
 *
 * Copyright (C) 2010 Fabien Chouteau <fabien.chouteau@barco.com>
 */


#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/usb/composite.h>
#include <linux/usb/g_hid.h>
#include <linux/miscdevice.h>
#include <linux/ioctl.h>
#include <linux/mtd/spi-nor.h>
#include <linux/sched.h>
#include <linux/aurga.h>

#define DRIVER_DESC		"Aurga Keyboard/Mouse/Touchscreen"
#define DRIVER_VERSION		"2010/03/16"
#define FW_VERSION	0x2004

#include "u_hid.h"

static void hid_device_release(struct device * dev);
int usb_test_mode = 0;
static struct work_struct load_nvram_task;

static int usb_function = 0; // 0: No Function 1: HID  2: Video

//*************************************************************************//

static struct hidg_func_descriptor qisi_keyboard = {
	.subclass	=	1, // No subclass
	.protocol	= 	1, // Keyboard
	.report_length	=	8,
	.report_desc_length	= 63,
	.report_desc = {
		0x05, 0x01,	/* USAGE_PAGE (Generic Desktop)	          */
		0x09, 0x06,	/* USAGE (Keyboard)                       */
		0xa1, 0x01,	/* COLLECTION (Application)               */
		0x05, 0x07,	/*   USAGE_PAGE (Keyboard)                */
		0x19, 0xe0,	/*   USAGE_MINIMUM (Keyboard LeftControl) */
		0x29, 0xe7,	/*   USAGE_MAXIMUM (Keyboard Right GUI)   */
		0x15, 0x00,	/*   LOGICAL_MINIMUM (0)                  */
		0x25, 0x01,	/*   LOGICAL_MAXIMUM (1)                  */
		0x75, 0x01,	/*   REPORT_SIZE (1)                      */
		0x95, 0x08,	/*   REPORT_COUNT (8)                     */
		0x81, 0x02,	/*   INPUT (Data,Var,Abs)                 */
		0x95, 0x01,	/*   REPORT_COUNT (1)                     */
		0x75, 0x08,	/*   REPORT_SIZE (8)                      */
		0x81, 0x03,	/*   INPUT (Cnst,Var,Abs)                 */
		0x95, 0x05,	/*   REPORT_COUNT (5)                     */
		0x75, 0x01,	/*   REPORT_SIZE (1)                      */
		0x05, 0x08,	/*   USAGE_PAGE (LEDs)                    */
		0x19, 0x01,	/*   USAGE_MINIMUM (Num Lock)             */
		0x29, 0x05,	/*   USAGE_MAXIMUM (Kana)                 */
		0x91, 0x02,	/*   OUTPUT (Data,Var,Abs)                */
		0x95, 0x01,	/*   REPORT_COUNT (1)                     */
		0x75, 0x03,	/*   REPORT_SIZE (3)                      */
		0x91, 0x03,	/*   OUTPUT (Cnst,Var,Abs)                */
		0x95, 0x06,	/*   REPORT_COUNT (6)                     */
		0x75, 0x08,	/*   REPORT_SIZE (8)                      */
		0x15, 0x00,	/*   LOGICAL_MINIMUM (0)                  */
		0x25, 0x65,	/*   LOGICAL_MAXIMUM (101)                */
		0x05, 0x07,	/*   USAGE_PAGE (Keyboard)                */
		0x19, 0x00,	/*   USAGE_MINIMUM (Reserved)             */
		0x29, 0x65,	/*   USAGE_MAXIMUM (Keyboard Application) */
		0x81, 0x00,	/*   INPUT (Data,Ary,Abs)                 */
		0xc0		/* END_COLLECTION                         */
	},
};

static struct platform_device qisi_hid_keyboard = {
	.name	=	"hidg",
	.id		=	0,
	.num_resources      	=	0,
	.resource       =	0,
	.dev.platform_data	=	&qisi_keyboard,
	.dev.release = hid_device_release,
};

static struct hidg_func_descriptor qisi_mouse = {
	.subclass	=	1, // No subclass
	.protocol	= 	2, // Mouse
	.report_length	=	4,
	.report_desc_length	= 52,
	.report_desc 	= {
		 0x05, 0x01,  //Usage Page(Generic Desktop Controls)
		 0x09, 0x02,  //Usage (Mouse)
		 0xa1, 0x01,  //Collction (Application)
		 0x09, 0x01,  //Usage (pointer)
		 0xa1, 0x00,  //Collction (Physical)
		 0x05, 0x09,  //Usage Page (Button)
		 0x19, 0x01,  //Usage Minimum(1)
		 0x29, 0x05,  //Usage Maximum(5)
		 0x15, 0x00,  //Logical Minimum(1)
		 0x25, 0x01,  //Logical Maximum(1)
		 0x95, 0x05,  //Report Count(5)
		 0x75, 0x01,  //Report Size(1)
		 0x81, 0x02,  //Input(Data,Variable,Absolute,BitField)
		 0x95, 0x01,  //Report Count(1)
		 0x75, 0x03,  //Report Size(3)
		 0x81, 0x01,  //Input(Constant,Array,Absolute,BitField)
		 0x05, 0x01,  //Usage Page(Generic Desktop Controls)
		 0x09, 0x30,  //Usage(x)
		 0x09, 0x31,  //Usage(y)
		 0x09, 0x38,  //Usage(Wheel)
		 0x15, 0x81,  //Logical Minimum(-127)
		 0x25, 0x7F,  //Logical Maximum(127)
		 0x75, 0x08,  //Report Size(8)
		 0x95, 0x03,  //Report Count(3)
		 0x81, 0x06,  //Input(Data,Variable,Relative,BitField)
		 0xc0,  //End Collection
		 0xc0  //End Collection
	},
};

static struct platform_device qisi_hid_mouse = {
	.name	=	"hidg",
	.id		=	1,
	.num_resources      	=	0,
	.resource       =	0,
	.dev.platform_data	=	&qisi_mouse,
	.dev.release = hid_device_release,
};

static struct hidg_func_descriptor qisi_mouse2 = {
	.subclass	=	1, // No subclass
	.protocol	= 	2, // Mouse
	.report_length	=	6,
	.report_desc_length	= 74,
	.report_desc 	= {
		0x05, 0x01,  // Usage Page (Generic Desktop)
		0x09, 0x02,  // Usage (Mouse)
		0xA1, 0x01,  // Collection (Application)
		0x09, 0x01,  // Usage (Pointer)
		0xA1, 0x00,  // Collection (Physical)
		0x05, 0x09,  // Usage Page (Button)
		0x19, 0x01,  // Usage Minimum (0x01)
		0x29, 0x03,  // Usage Maximum (0x03)
		0x15, 0x00,  // Logical Minimum (0)
		0x25, 0x01,  // Logical Maximum (1)
		0x95, 0x03,  // Report Count (3)
		0x75, 0x01,  // Report Size (1)
		0x81, 0x02,  // Input (Data, Variable, Absolute)
		0x95, 0x01,  // Report Count (1)
		0x75, 0x05,  // Report Size (5)
		0x81, 0x01,  // Input (Constant, Array, Absolute)
		0x05, 0x01,  // Usage Page (Generic Desktop)
		0x09, 0x30,  // Usage (X)
		0x09, 0x31,  // Usage (Y)
		0x15, 0x00,  // Logical Minimum (0)
		0x26, 0xFF, 0x7F,  // Logical Maximum (32767)
		0x35, 0x00,  // Physical Minimum (0)
		0x46, 0xFF, 0x7F,  // Physical Maximum (32767)
		0x75, 0x10,  // Report Size (16)
		0x95, 0x02,  // Report Count (2)
		0x81, 0x02,  // Input (Data, Variable, Absolute)
		0x05, 0x01,  // Usage Page (Generic Desktop)
		0x09, 0x38,  // Usage (Wheel)
		0x15, 0x81,  // Logical Minimum (-127)
		0x25, 0x7F,  // Logical Maximum (127)
		0x35, 0x00,  // Physical Minimum (0)
		0x45, 0x00,  // Physical Maximum (0)
		0x75, 0x08,  // Report Size (8)
		0x95, 0x01,  // Report Count (1)
		0x81, 0x06,  // Input (Data, Variable, Relative)
		0xC0,  // End Collection
		0xC0,  // End Collection
	},
};

static struct platform_device qisi_hid_mouse2 = {
	.name	=	"hidg",
	.id		=	3,
	.num_resources      	=	0,
	.resource       =	0,
	.dev.platform_data	=	&qisi_mouse2,
	.dev.release = hid_device_release,
};


// http://www.mamicode.com/info-detail-115455.html
static unsigned char desc_touchscreen[] = 
{
		0x05, 0x0d,                         // USAGE_PAGE (Digitizers)          
		0x09, 0x04,                         // USAGE (Touch Screen)             
		0xa1, 0x01,                         // COLLECTION (Application)         
		0x85, 0x04,                         //   REPORT_ID (Touch)              
		0x09, 0x22,                         //   USAGE (Finger)                 
		0xa1, 0x02,                         //     COLLECTION (Logical)  
		0x09, 0x42,                         //       USAGE (Tip Switch)           
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)          
		0x25, 0x01,                         //       LOGICAL_MAXIMUM (1)          
		0x75, 0x01,                         //       REPORT_SIZE (1)              
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x95, 0x07,                         //       REPORT_COUNT (7)  
		0x81, 0x03,                         //       INPUT (Cnst,Ary,Abs)
		0x75, 0x08,                         //       REPORT_SIZE (8)
		0x09, 0x51,                         //       USAGE (Contact Identifier)
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x05, 0x01,                         //       USAGE_PAGE (Generic Desk..
		0x26, 0xff, 0x0f,                   //       LOGICAL_MAXIMUM (4095)         
		0x75, 0x10,                         //       REPORT_SIZE (16)             
		0x55, 0x0e,                         //       UNIT_EXPONENT (-2)           
		0x65, 0x13,                         //       UNIT(Inch,EngLinear)                  
		0x09, 0x30,                         //       USAGE (X)                    
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x46, 0xb5, 0x04,                   //       PHYSICAL_MAXIMUM (1205)
		0x95, 0x02,                         //       REPORT_COUNT (2)         
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)         
		0x46, 0x8a, 0x03,                   //       PHYSICAL_MAXIMUM (906)
		0x09, 0x31,                         //       USAGE (Y)                    
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x05, 0x0d,                         //       USAGE_PAGE (Digitizers)
		0x09, 0x48,                         //       USAGE (Width)                
		0x09, 0x49,                         //       USAGE (Height)               
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x95, 0x01,                         //       REPORT_COUNT (1)
		0x55, 0x0C,                         //       UNIT_EXPONENT (-4)           
		0x65, 0x12,                         //       UNIT (Radians,SIROtation)        
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x47, 0x6f, 0xf5, 0x00, 0x00,       //       PHYSICAL_MAXIMUM (62831)      
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)      
		0x27, 0x6f, 0xf5, 0x00, 0x00,       //       LOGICAL_MAXIMUM (62831)        
		0x09, 0x3f,                         //       USAGE (Azimuth[Orientation]) 
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)  
		0xc0,                               //     END_COLLECTION

		0x09, 0x22,                         //   USAGE (Finger)                 
		0xa1, 0x02,                         //     COLLECTION (Logical)  
		0x09, 0x42,                         //       USAGE (Tip Switch)           
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)          
		0x25, 0x01,                         //       LOGICAL_MAXIMUM (1)          
		0x75, 0x01,                         //       REPORT_SIZE (1)              
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x95, 0x07,                         //       REPORT_COUNT (7)  
		0x81, 0x03,                         //       INPUT (Cnst,Ary,Abs)
		0x75, 0x08,                         //       REPORT_SIZE (8)
		0x09, 0x51,                         //       USAGE (Contact Identifier)
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x05, 0x01,                         //       USAGE_PAGE (Generic Desk..
		0x26, 0xff, 0x0f,                   //       LOGICAL_MAXIMUM (4095)         
		0x75, 0x10,                         //       REPORT_SIZE (16)             
		0x55, 0x0e,                         //       UNIT_EXPONENT (-2)           
		0x65, 0x13,                         //       UNIT(Inch,EngLinear)                  
		0x09, 0x30,                         //       USAGE (X)                    
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x46, 0xb5, 0x04,                   //       PHYSICAL_MAXIMUM (1205)
		0x95, 0x02,                         //       REPORT_COUNT (2)         
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)         
		0x46, 0x8a, 0x03,                   //       PHYSICAL_MAXIMUM (906)
		0x09, 0x31,                         //       USAGE (Y)                    
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x05, 0x0d,                         //       USAGE_PAGE (Digitizers)
		0x09, 0x48,                         //       USAGE (Width)                
		0x09, 0x49,                         //       USAGE (Height)               
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x95, 0x01,                         //       REPORT_COUNT (1)
		0x55, 0x0C,                         //       UNIT_EXPONENT (-4)           
		0x65, 0x12,                         //       UNIT (Radians,SIROtation)        
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x47, 0x6f, 0xf5, 0x00, 0x00,       //       PHYSICAL_MAXIMUM (62831)      
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)      
		0x27, 0x6f, 0xf5, 0x00, 0x00,       //       LOGICAL_MAXIMUM (62831)        
		0x09, 0x3f,                         //       USAGE (Azimuth[Orientation]) 
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)  
		0xc0,                               //     END_COLLECTION

		0x09, 0x22,                         //   USAGE (Finger)                 
		0xa1, 0x02,                         //     COLLECTION (Logical)  
		0x09, 0x42,                         //       USAGE (Tip Switch)           
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)          
		0x25, 0x01,                         //       LOGICAL_MAXIMUM (1)          
		0x75, 0x01,                         //       REPORT_SIZE (1)              
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x95, 0x07,                         //       REPORT_COUNT (7)  
		0x81, 0x03,                         //       INPUT (Cnst,Ary,Abs)
		0x75, 0x08,                         //       REPORT_SIZE (8)
		0x09, 0x51,                         //       USAGE (Contact Identifier)
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x05, 0x01,                         //       USAGE_PAGE (Generic Desk..
		0x26, 0xff, 0x0f,                   //       LOGICAL_MAXIMUM (4095)         
		0x75, 0x10,                         //       REPORT_SIZE (16)             
		0x55, 0x0e,                         //       UNIT_EXPONENT (-2)           
		0x65, 0x13,                         //       UNIT(Inch,EngLinear)                  
		0x09, 0x30,                         //       USAGE (X)                    
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x46, 0xb5, 0x04,                   //       PHYSICAL_MAXIMUM (1205)
		0x95, 0x02,                         //       REPORT_COUNT (2)         
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)         
		0x46, 0x8a, 0x03,                   //       PHYSICAL_MAXIMUM (906)
		0x09, 0x31,                         //       USAGE (Y)                    
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x05, 0x0d,                         //       USAGE_PAGE (Digitizers)
		0x09, 0x48,                         //       USAGE (Width)                
		0x09, 0x49,                         //       USAGE (Height)               
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x95, 0x01,                         //       REPORT_COUNT (1)
		0x55, 0x0C,                         //       UNIT_EXPONENT (-4)           
		0x65, 0x12,                         //       UNIT (Radians,SIROtation)        
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x47, 0x6f, 0xf5, 0x00, 0x00,       //       PHYSICAL_MAXIMUM (62831)      
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)      
		0x27, 0x6f, 0xf5, 0x00, 0x00,       //       LOGICAL_MAXIMUM (62831)        
		0x09, 0x3f,                         //       USAGE (Azimuth[Orientation]) 
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)  
		0xc0,                               //     END_COLLECTION

		0x09, 0x22,                         //   USAGE (Finger)                 
		0xa1, 0x02,                         //     COLLECTION (Logical)  
		0x09, 0x42,                         //       USAGE (Tip Switch)           
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)          
		0x25, 0x01,                         //       LOGICAL_MAXIMUM (1)          
		0x75, 0x01,                         //       REPORT_SIZE (1)              
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x95, 0x07,                         //       REPORT_COUNT (7)  
		0x81, 0x03,                         //       INPUT (Cnst,Ary,Abs)
		0x75, 0x08,                         //       REPORT_SIZE (8)
		0x09, 0x51,                         //       USAGE (Contact Identifier)
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x05, 0x01,                         //       USAGE_PAGE (Generic Desk..
		0x26, 0xff, 0x0f,                   //       LOGICAL_MAXIMUM (4095)         
		0x75, 0x10,                         //       REPORT_SIZE (16)             
		0x55, 0x0e,                         //       UNIT_EXPONENT (-2)           
		0x65, 0x13,                         //       UNIT(Inch,EngLinear)                  
		0x09, 0x30,                         //       USAGE (X)                    
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x46, 0xb5, 0x04,                   //       PHYSICAL_MAXIMUM (1205)
		0x95, 0x02,                         //       REPORT_COUNT (2)         
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)         
		0x46, 0x8a, 0x03,                   //       PHYSICAL_MAXIMUM (906)
		0x09, 0x31,                         //       USAGE (Y)                    
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x05, 0x0d,                         //       USAGE_PAGE (Digitizers)
		0x09, 0x48,                         //       USAGE (Width)                
		0x09, 0x49,                         //       USAGE (Height)               
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x95, 0x01,                         //       REPORT_COUNT (1)
		0x55, 0x0C,                         //       UNIT_EXPONENT (-4)           
		0x65, 0x12,                         //       UNIT (Radians,SIROtation)        
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x47, 0x6f, 0xf5, 0x00, 0x00,       //       PHYSICAL_MAXIMUM (62831)      
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)      
		0x27, 0x6f, 0xf5, 0x00, 0x00,       //       LOGICAL_MAXIMUM (62831)        
		0x09, 0x3f,                         //       USAGE (Azimuth[Orientation]) 
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)  
		0xc0,                               //     END_COLLECTION

		0x05, 0x0d,                         //   USAGE_PAGE (Digitizers)
		0x55, 0x0C,                         //     UNIT_EXPONENT (-4)           
		0x66, 0x01, 0x10,                   //     UNIT (Seconds)        
		0x47, 0xff, 0xff, 0x00, 0x00,       //       PHYSICAL_MAXIMUM (65535)
		0x27, 0xff, 0xff, 0x00, 0x00,       //   LOGICAL_MAXIMUM (65535) 
		0x75, 0x10,                         //   REPORT_SIZE (16)             
		0x95, 0x01,                         //   REPORT_COUNT (1) 
		0x09, 0x56,                         //   USAGE (Scan Time)
		0x81, 0x02,                         //   INPUT (Data,Var,Abs)         
		0x09, 0x54,                         //   USAGE (Contact count)
		0x25, 0x7f,                         //   LOGICAL_MAXIMUM (127) 
		0x95, 0x01,                         //   REPORT_COUNT (1)
		0x75, 0x08,                         //   REPORT_SIZE (8)    
		0x81, 0x02,                         //   INPUT (Data,Var,Abs)
		0x85, 0x05,          			 	//   REPORT_ID (Feature)              
		0x09, 0x55,                         //   USAGE(Contact Count Maximum)
		0x95, 0x01,                         //   REPORT_COUNT (1)
		0x25, 0x04,                         //   LOGICAL_MAXIMUM (4)
		0xb1, 0x02,                         //   FEATURE (Data,Var,Abs)
		0x85, 0x44,                         //   REPORT_ID (Feature)
		0x06, 0x00, 0xff,                   //   USAGE_PAGE (Vendor Defined)  
		0x09, 0xC5,                         //   USAGE (Vendor Usage 0xC5)    
		0x15, 0x00,                         //   LOGICAL_MINIMUM (0)          
		0x26, 0xff, 0x00,                   //   LOGICAL_MAXIMUM (0xff) 
		0x75, 0x08,                         //   REPORT_SIZE (8)             
		0x96, 0x00,  0x01,                  //   REPORT_COUNT (0x100 (256))             
		0xb1, 0x02,                         //   FEATURE (Data,Var,Abs) 
		0xc0,                               // END_COLLECTION 

		0x05, 0x0D,                    // USAGE_PAGE(Digitizers)
        0x09, 0x04,                    // USAGE     (Touch Screen)
        0xA1, 0x01,                    // COLLECTION(Application)
		0x85, 0x06,                    //   REPORT_ID (Touch) 		
        // define the maximum amount of fingers that the device supports
        0x09, 0x55,                    //   USAGE(Contact Count Maximum)
        0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
        0xB1, 0x02,                    //   FEATURE (Data,Var,Abs)2w 56

        // define the actual amount of fingers that are concurrently touching the screen
        0x09, 0x54,                    //   USAGE (Contact count)
        0x95, 0x01,                    //   REPORT_COUNT(1)
        0x75, 0x08,                    //   REPORT_SIZE (8)
        0x81, 0x02,                    //   INPUT (Data,Var,Abs)
		
		// declare a finger collection
        0x09, 0x22,                    //   USAGE (Finger)
        0xA1, 0x02,                    //   COLLECTION (Logical)

        // declare an identifier for the finger
        0x09, 0x51,                    //     USAGE (Contact Identifier)
        0x75, 0x08,                    //     REPORT_SIZE (8)
        0x95, 0x01,                    //     REPORT_COUNT (1)
        0x81, 0x02,                    //     INPUT (Data,Var,Abs)

        // declare Tip Switch and In Range
        0x09, 0x42,                    //     USAGE (Tip Switch)
        0x09, 0x32,                    //     USAGE (In Range)
        0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
        0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
        0x75, 0x01,                    //     REPORT_SIZE (1)
        0x95, 0x02,                    //     REPORT_COUNT(2)
        0x81, 0x02,                    //     INPUT (Data,Var,Abs)

        // declare the remaining 6 bits of the first data byte as constant -> the driver will ignore them
        0x95, 0x06,                    //     REPORT_COUNT (6)
        0x81, 0x03,                    //     INPUT (Cnst,Ary,Abs)

        // define absolute X and Y coordinates of 16 bit each (percent values multiplied with 100)
        0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
        0x09, 0x30,                    //     Usage (X)
        0x09, 0x31,                    //     Usage (Y)
        0x16, 0x00, 0x00,              //     Logical Minimum (0)
        0x26, 0xff, 0x0f,              //     Logical Maximum (4095)
        0x36, 0x00, 0x00,              //     Physical Minimum (0)
        0x46, 0xff, 0x0f,              //     Physical Maximum (4095)
        0x66, 0x00, 0x00,              //     UNIT (None)
        0x75, 0x10,                    //     Report Size (16),
        0x95, 0x02,                    //     Report Count (2),
        0x81, 0x02,                    //     Input (Data,Var,Abs)
        0xC0,                          //   END_COLLECTION
        0xC0,                           // END_COLLECTION

		// Integrated Windows Pen TLC
		0x05, 0x0d,                         // USAGE_PAGE (Digitizers)          
		0x09, 0x02,                         // USAGE (Pen)                      
		0xa1, 0x01,                         // COLLECTION (Application)         
		0x85, 0x10,                 //   REPORT_ID (Pen)                
		0x09, 0x20,                         //   USAGE (Stylus)                 
		0xa1, 0x00,                         //   COLLECTION (Physical)          
		0x09, 0x42,                         //     USAGE (Tip Switch)           
		0x09, 0x44,                         //     USAGE (Barrel Switch)        
		0x09, 0x3c,                         //     USAGE (Invert)               
		0x09, 0x45,                         //     USAGE (Eraser Switch)        
		0x15, 0x00,                         //     LOGICAL_MINIMUM (0)          
		0x25, 0x01,                         //     LOGICAL_MAXIMUM (1)          
		0x75, 0x01,                         //     REPORT_SIZE (1)              
		0x95, 0x04,                         //     REPORT_COUNT (4)             
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0x95, 0x01,                         //     REPORT_COUNT (1)             
		0x81, 0x03,                         //     INPUT (Cnst,Var,Abs)         
		0x09, 0x32,                         //     USAGE (In Range)             
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0x95, 0x02,                         //     REPORT_COUNT (2)             
		0x81, 0x03,                         //     INPUT (Cnst,Var,Abs)         
		0x05, 0x01,                         //     USAGE_PAGE (Generic Desktop) 
		0x09, 0x30,                         //     USAGE (X)                    
		0x75, 0x10,                         //     REPORT_SIZE (16)             
		0x95, 0x01,                         //     REPORT_COUNT (1)             
		0xa4,                               //     PUSH                         
		0x55, 0x0d,                         //     UNIT_EXPONENT (-3)           
		0x65, 0x13,                         //     UNIT (Inch,EngLinear)        
		0x35, 0x00,                         //     PHYSICAL_MINIMUM (0)         
		0x46, 0x3a, 0x20,                   //     PHYSICAL_MAXIMUM (8250)      
		0x26, 0xf8, 0x52,                   //     LOGICAL_MAXIMUM (21240)      
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0x09, 0x31,                         //     USAGE (Y)                    
		0x46, 0x2c, 0x18,                   //     PHYSICAL_MAXIMUM (6188)      
		0x26, 0x6c, 0x3e,                   //     LOGICAL_MAXIMUM (15980)      
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0xb4,                               //     POP                          
		0x05, 0x0d,                         //     USAGE_PAGE (Digitizers)      
		0x09, 0x30,                         //     USAGE (Tip Pressure)         
		0x26, 0x00, 0x10,                   //     LOGICAL_MAXIMUM (4096)        
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0x75, 0x10,                         //     REPORT_SIZE (16)              
		0x09, 0x3d,                         //     USAGE (X Tilt)               
		0x16, 0x01, 0x80,                   //     LOGICAL_MINIMUM (-32767)       
		0x26, 0xff, 0x7F,                   //     LOGICAL_MAXIMUM (32767)        
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0x09, 0x3e,                         //     USAGE (Y Tilt)               
		0x16, 0x01, 0x80,                   //     LOGICAL_MINIMUM (-32767)       
		0x26, 0xff, 0x7F,                   //     LOGICAL_MAXIMUM (32767)        
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0xc0,                               //   END_COLLECTION                 
		0xc0,                                // END_COLLECTION

		0x06, 0xC0, 0xFF,  // Usage Page (Vendor Defined 0xFFC0)
		0x09, 0x01,        // Usage (0x01)
		0xA1, 0x01,        // Collection (Application)
		0x85, 0x0C,        //   Report ID (12)
		0x09, 0x02,        //   Usage (0x02)
		0x15, 0x00,        //   Logical Minimum (0)
		0x26, 0xFF, 0x00,  //   Logical Maximum (255)
		0x75, 0x08,        //   Report Size (8)
		0x95, 0xFF,        //   Report Count (63)
		0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
		0xC0,              // End Collection
	};

static unsigned char desc_touchscreen_11[] = 
{
		0x05, 0x0d,                         // USAGE_PAGE (Digitizers)          
		0x09, 0x04,                         // USAGE (Touch Screen)             
		0xa1, 0x01,                         // COLLECTION (Application)         
		0x85, 0x04,                         //   REPORT_ID (Touch)              
		0x09, 0x22,                         //   USAGE (Finger)                 
		0xa1, 0x02,                         //     COLLECTION (Logical)  
		0x09, 0x42,                         //       USAGE (Tip Switch)           
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)          
		0x25, 0x01,                         //       LOGICAL_MAXIMUM (1)          
		0x75, 0x01,                         //       REPORT_SIZE (1)              
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x95, 0x07,                         //       REPORT_COUNT (7)  
		0x81, 0x03,                         //       INPUT (Cnst,Ary,Abs)
		0x75, 0x08,                         //       REPORT_SIZE (8)
		0x09, 0x51,                         //       USAGE (Contact Identifier)
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x05, 0x01,                         //       USAGE_PAGE (Generic Desk..
		0x26, 0xff, 0x0f,                   //       LOGICAL_MAXIMUM (4095)         
		0x75, 0x10,                         //       REPORT_SIZE (16)             
		0x55, 0x0e,                         //       UNIT_EXPONENT (-2)           
		0x65, 0x13,                         //       UNIT(Inch,EngLinear)                  
		0x09, 0x30,                         //       USAGE (X)                    
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x46, 0xb5, 0x04,                   //       PHYSICAL_MAXIMUM (1205)
		0x95, 0x02,                         //       REPORT_COUNT (2)         
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)         
		0x46, 0x8a, 0x03,                   //       PHYSICAL_MAXIMUM (906)
		0x09, 0x31,                         //       USAGE (Y)                    
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x05, 0x0d,                         //       USAGE_PAGE (Digitizers)
		0x09, 0x48,                         //       USAGE (Width)                
		0x09, 0x49,                         //       USAGE (Height)               
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x95, 0x01,                         //       REPORT_COUNT (1)
		0x55, 0x0C,                         //       UNIT_EXPONENT (-4)           
		0x65, 0x12,                         //       UNIT (Radians,SIROtation)        
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x47, 0x6f, 0xf5, 0x00, 0x00,       //       PHYSICAL_MAXIMUM (62831)      
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)      
		0x27, 0x6f, 0xf5, 0x00, 0x00,       //       LOGICAL_MAXIMUM (62831)        
		0x09, 0x3f,                         //       USAGE (Azimuth[Orientation]) 
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)  
		0xc0,                               //     END_COLLECTION

		0x09, 0x22,                         //   USAGE (Finger)                 
		0xa1, 0x02,                         //     COLLECTION (Logical)  
		0x09, 0x42,                         //       USAGE (Tip Switch)           
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)          
		0x25, 0x01,                         //       LOGICAL_MAXIMUM (1)          
		0x75, 0x01,                         //       REPORT_SIZE (1)              
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x95, 0x07,                         //       REPORT_COUNT (7)  
		0x81, 0x03,                         //       INPUT (Cnst,Ary,Abs)
		0x75, 0x08,                         //       REPORT_SIZE (8)
		0x09, 0x51,                         //       USAGE (Contact Identifier)
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x05, 0x01,                         //       USAGE_PAGE (Generic Desk..
		0x26, 0xff, 0x0f,                   //       LOGICAL_MAXIMUM (4095)         
		0x75, 0x10,                         //       REPORT_SIZE (16)             
		0x55, 0x0e,                         //       UNIT_EXPONENT (-2)           
		0x65, 0x13,                         //       UNIT(Inch,EngLinear)                  
		0x09, 0x30,                         //       USAGE (X)                    
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x46, 0xb5, 0x04,                   //       PHYSICAL_MAXIMUM (1205)
		0x95, 0x02,                         //       REPORT_COUNT (2)         
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)         
		0x46, 0x8a, 0x03,                   //       PHYSICAL_MAXIMUM (906)
		0x09, 0x31,                         //       USAGE (Y)                    
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x05, 0x0d,                         //       USAGE_PAGE (Digitizers)
		0x09, 0x48,                         //       USAGE (Width)                
		0x09, 0x49,                         //       USAGE (Height)               
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x95, 0x01,                         //       REPORT_COUNT (1)
		0x55, 0x0C,                         //       UNIT_EXPONENT (-4)           
		0x65, 0x12,                         //       UNIT (Radians,SIROtation)        
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x47, 0x6f, 0xf5, 0x00, 0x00,       //       PHYSICAL_MAXIMUM (62831)      
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)      
		0x27, 0x6f, 0xf5, 0x00, 0x00,       //       LOGICAL_MAXIMUM (62831)        
		0x09, 0x3f,                         //       USAGE (Azimuth[Orientation]) 
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)  
		0xc0,                               //     END_COLLECTION

		0x09, 0x22,                         //   USAGE (Finger)                 
		0xa1, 0x02,                         //     COLLECTION (Logical)  
		0x09, 0x42,                         //       USAGE (Tip Switch)           
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)          
		0x25, 0x01,                         //       LOGICAL_MAXIMUM (1)          
		0x75, 0x01,                         //       REPORT_SIZE (1)              
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x95, 0x07,                         //       REPORT_COUNT (7)  
		0x81, 0x03,                         //       INPUT (Cnst,Ary,Abs)
		0x75, 0x08,                         //       REPORT_SIZE (8)
		0x09, 0x51,                         //       USAGE (Contact Identifier)
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x05, 0x01,                         //       USAGE_PAGE (Generic Desk..
		0x26, 0xff, 0x0f,                   //       LOGICAL_MAXIMUM (4095)         
		0x75, 0x10,                         //       REPORT_SIZE (16)             
		0x55, 0x0e,                         //       UNIT_EXPONENT (-2)           
		0x65, 0x13,                         //       UNIT(Inch,EngLinear)                  
		0x09, 0x30,                         //       USAGE (X)                    
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x46, 0xb5, 0x04,                   //       PHYSICAL_MAXIMUM (1205)
		0x95, 0x02,                         //       REPORT_COUNT (2)         
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)         
		0x46, 0x8a, 0x03,                   //       PHYSICAL_MAXIMUM (906)
		0x09, 0x31,                         //       USAGE (Y)                    
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x05, 0x0d,                         //       USAGE_PAGE (Digitizers)
		0x09, 0x48,                         //       USAGE (Width)                
		0x09, 0x49,                         //       USAGE (Height)               
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x95, 0x01,                         //       REPORT_COUNT (1)
		0x55, 0x0C,                         //       UNIT_EXPONENT (-4)           
		0x65, 0x12,                         //       UNIT (Radians,SIROtation)        
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x47, 0x6f, 0xf5, 0x00, 0x00,       //       PHYSICAL_MAXIMUM (62831)      
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)      
		0x27, 0x6f, 0xf5, 0x00, 0x00,       //       LOGICAL_MAXIMUM (62831)        
		0x09, 0x3f,                         //       USAGE (Azimuth[Orientation]) 
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)  
		0xc0,                               //     END_COLLECTION

		0x09, 0x22,                         //   USAGE (Finger)                 
		0xa1, 0x02,                         //     COLLECTION (Logical)  
		0x09, 0x42,                         //       USAGE (Tip Switch)           
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)          
		0x25, 0x01,                         //       LOGICAL_MAXIMUM (1)          
		0x75, 0x01,                         //       REPORT_SIZE (1)              
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x95, 0x07,                         //       REPORT_COUNT (7)  
		0x81, 0x03,                         //       INPUT (Cnst,Ary,Abs)
		0x75, 0x08,                         //       REPORT_SIZE (8)
		0x09, 0x51,                         //       USAGE (Contact Identifier)
		0x95, 0x01,                         //       REPORT_COUNT (1)             
		0x81, 0x02,                         //       INPUT (Data,Var,Abs) 
		0x05, 0x01,                         //       USAGE_PAGE (Generic Desk..
		0x26, 0xff, 0x0f,                   //       LOGICAL_MAXIMUM (4095)         
		0x75, 0x10,                         //       REPORT_SIZE (16)             
		0x55, 0x0e,                         //       UNIT_EXPONENT (-2)           
		0x65, 0x13,                         //       UNIT(Inch,EngLinear)                  
		0x09, 0x30,                         //       USAGE (X)                    
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x46, 0xb5, 0x04,                   //       PHYSICAL_MAXIMUM (1205)
		0x95, 0x02,                         //       REPORT_COUNT (2)         
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)         
		0x46, 0x8a, 0x03,                   //       PHYSICAL_MAXIMUM (906)
		0x09, 0x31,                         //       USAGE (Y)                    
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x05, 0x0d,                         //       USAGE_PAGE (Digitizers)
		0x09, 0x48,                         //       USAGE (Width)                
		0x09, 0x49,                         //       USAGE (Height)               
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)
		0x95, 0x01,                         //       REPORT_COUNT (1)
		0x55, 0x0C,                         //       UNIT_EXPONENT (-4)           
		0x65, 0x12,                         //       UNIT (Radians,SIROtation)        
		0x35, 0x00,                         //       PHYSICAL_MINIMUM (0)         
		0x47, 0x6f, 0xf5, 0x00, 0x00,       //       PHYSICAL_MAXIMUM (62831)      
		0x15, 0x00,                         //       LOGICAL_MINIMUM (0)      
		0x27, 0x6f, 0xf5, 0x00, 0x00,       //       LOGICAL_MAXIMUM (62831)        
		0x09, 0x3f,                         //       USAGE (Azimuth[Orientation]) 
		0x81, 0x02,                         //       INPUT (Data,Var,Abs)  
		0xc0,                               //     END_COLLECTION

		0x05, 0x0d,                         //   USAGE_PAGE (Digitizers)
		0x55, 0x0C,                         //     UNIT_EXPONENT (-4)           
		0x66, 0x01, 0x10,                   //     UNIT (Seconds)        
		0x47, 0xff, 0xff, 0x00, 0x00,       //       PHYSICAL_MAXIMUM (65535)
		0x27, 0xff, 0xff, 0x00, 0x00,       //   LOGICAL_MAXIMUM (65535) 
		0x75, 0x10,                         //   REPORT_SIZE (16)             
		0x95, 0x01,                         //   REPORT_COUNT (1) 
		0x09, 0x56,                         //   USAGE (Scan Time)
		0x81, 0x02,                         //   INPUT (Data,Var,Abs)         
		0x09, 0x54,                         //   USAGE (Contact count)
		0x25, 0x7f,                         //   LOGICAL_MAXIMUM (127) 
		0x95, 0x01,                         //   REPORT_COUNT (1)
		0x75, 0x08,                         //   REPORT_SIZE (8)    
		0x81, 0x02,                         //   INPUT (Data,Var,Abs)
		0x85, 0x05,          			 	//   REPORT_ID (Feature)              
		0x09, 0x55,                         //   USAGE(Contact Count Maximum)
		0x95, 0x01,                         //   REPORT_COUNT (1)
		0x25, 0x04,                         //   LOGICAL_MAXIMUM (4)
		0xb1, 0x02,                         //   FEATURE (Data,Var,Abs)
		// 0x85, 0x44,                         //   REPORT_ID (Feature)
		// 0x06, 0x00, 0xff,                   //   USAGE_PAGE (Vendor Defined)  
		// 0x09, 0xC5,                         //   USAGE (Vendor Usage 0xC5)    
		// 0x15, 0x00,                         //   LOGICAL_MINIMUM (0)          
		// 0x26, 0xff, 0x00,                   //   LOGICAL_MAXIMUM (0xff) 
		// 0x75, 0x08,                         //   REPORT_SIZE (8)             
		// 0x96, 0x00,  0x01,                  //   REPORT_COUNT (0x100 (256))             
		// 0xb1, 0x02,                         //   FEATURE (Data,Var,Abs) 
		0xc0,                               // END_COLLECTION 

		0x05, 0x0D,                    // USAGE_PAGE(Digitizers)
        0x09, 0x04,                    // USAGE     (Touch Screen)
        0xA1, 0x01,                    // COLLECTION(Application)
		0x85, 0x06,                    //   REPORT_ID (Touch) 		
        // define the maximum amount of fingers that the device supports
        0x09, 0x55,                    //   USAGE(Contact Count Maximum)
        0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
        0xB1, 0x02,                    //   FEATURE (Data,Var,Abs)2w 56

        // define the actual amount of fingers that are concurrently touching the screen
        0x09, 0x54,                    //   USAGE (Contact count)
        0x95, 0x01,                    //   REPORT_COUNT(1)
        0x75, 0x08,                    //   REPORT_SIZE (8)
        0x81, 0x02,                    //   INPUT (Data,Var,Abs)
		
		// declare a finger collection
        0x09, 0x22,                    //   USAGE (Finger)
        0xA1, 0x02,                    //   COLLECTION (Logical)

        // declare an identifier for the finger
        0x09, 0x51,                    //     USAGE (Contact Identifier)
        0x75, 0x08,                    //     REPORT_SIZE (8)
        0x95, 0x01,                    //     REPORT_COUNT (1)
        0x81, 0x02,                    //     INPUT (Data,Var,Abs)

        // declare Tip Switch and In Range
        0x09, 0x42,                    //     USAGE (Tip Switch)
        0x09, 0x32,                    //     USAGE (In Range)
        0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
        0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
        0x75, 0x01,                    //     REPORT_SIZE (1)
        0x95, 0x02,                    //     REPORT_COUNT(2)
        0x81, 0x02,                    //     INPUT (Data,Var,Abs)

        // declare the remaining 6 bits of the first data byte as constant -> the driver will ignore them
        0x95, 0x06,                    //     REPORT_COUNT (6)
        0x81, 0x03,                    //     INPUT (Cnst,Ary,Abs)

        // define absolute X and Y coordinates of 16 bit each (percent values multiplied with 100)
        0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
        0x09, 0x30,                    //     Usage (X)
        0x09, 0x31,                    //     Usage (Y)
        0x16, 0x00, 0x00,              //     Logical Minimum (0)
        0x26, 0xff, 0x0f,              //     Logical Maximum (4095)
        0x36, 0x00, 0x00,              //     Physical Minimum (0)
        0x46, 0xff, 0x0f,              //     Physical Maximum (4095)
        0x66, 0x00, 0x00,              //     UNIT (None)
        0x75, 0x10,                    //     Report Size (16),
        0x95, 0x02,                    //     Report Count (2),
        0x81, 0x02,                    //     Input (Data,Var,Abs)
        0xC0,                          //   END_COLLECTION
        0xC0,                           // END_COLLECTION

		// Integrated Windows Pen TLC
		0x05, 0x0d,                         // USAGE_PAGE (Digitizers)          
		0x09, 0x02,                         // USAGE (Pen)                      
		0xa1, 0x01,                         // COLLECTION (Application)         
		0x85, 0x10,                 		//   REPORT_ID (Pen)                
		0x09, 0x20,                         //   USAGE (Stylus)                 
		0xa1, 0x00,                         //   COLLECTION (Physical)          
		0x09, 0x42,                         //     USAGE (Tip Switch)           
		0x09, 0x44,                         //     USAGE (Barrel Switch)        
		0x09, 0x3c,                         //     USAGE (Invert)               
		0x09, 0x45,                         //     USAGE (Eraser Switch)        
		0x15, 0x00,                         //     LOGICAL_MINIMUM (0)          
		0x25, 0x01,                         //     LOGICAL_MAXIMUM (1)          
		0x75, 0x01,                         //     REPORT_SIZE (1)              
		0x95, 0x04,                         //     REPORT_COUNT (4)             
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0x95, 0x01,                         //     REPORT_COUNT (1)             
		0x81, 0x03,                         //     INPUT (Cnst,Var,Abs)         
		0x09, 0x32,                         //     USAGE (In Range)             
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0x95, 0x02,                         //     REPORT_COUNT (2)             
		0x81, 0x03,                         //     INPUT (Cnst,Var,Abs)         
		0x05, 0x01,                         //     USAGE_PAGE (Generic Desktop) 
		0x09, 0x30,                         //     USAGE (X)                    
		0x75, 0x10,                         //     REPORT_SIZE (16)             
		0x95, 0x01,                         //     REPORT_COUNT (1)             
		0xa4,                               //     PUSH                         
		0x55, 0x0d,                         //     UNIT_EXPONENT (-3)           
		0x65, 0x13,                         //     UNIT (Inch,EngLinear)        
		0x35, 0x00,                         //     PHYSICAL_MINIMUM (0)         
		0x46, 0x3a, 0x20,                   //     PHYSICAL_MAXIMUM (8250)      
		0x26, 0xf8, 0x52,                   //     LOGICAL_MAXIMUM (21240)      
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0x09, 0x31,                         //     USAGE (Y)                    
		0x46, 0x2c, 0x18,                   //     PHYSICAL_MAXIMUM (6188)      
		0x26, 0x6c, 0x3e,                   //     LOGICAL_MAXIMUM (15980)      
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0xb4,                               //     POP                          
		0x05, 0x0d,                         //     USAGE_PAGE (Digitizers)      
		0x09, 0x30,                         //     USAGE (Tip Pressure)         
		0x26, 0x00, 0x10,                   //     LOGICAL_MAXIMUM (4096)        
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0x75, 0x10,                         //     REPORT_SIZE (16)              
		0x09, 0x3d,                         //     USAGE (X Tilt)               
		0x16, 0x01, 0x80,                   //     LOGICAL_MINIMUM (-32767)       
		0x26, 0xff, 0x7F,                   //     LOGICAL_MAXIMUM (32767)        
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0x09, 0x3e,                         //     USAGE (Y Tilt)               
		0x16, 0x01, 0x80,                   //     LOGICAL_MINIMUM (-32767)       
		0x26, 0xff, 0x7F,                   //     LOGICAL_MAXIMUM (32767)        
		0x81, 0x02,                         //     INPUT (Data,Var,Abs)         
		0xc0,                               //   END_COLLECTION                 
		0xc0,                                // END_COLLECTION

		0x06, 0xC0, 0xFF,  // Usage Page (Vendor Defined 0xFFC0)
		0x09, 0x01,        // Usage (0x01)
		0xA1, 0x01,        // Collection (Application)
		0x85, 0x0C,        //   Report ID (12)
		0x09, 0x02,        //   Usage (0x02)
		0x15, 0x00,        //   Logical Minimum (0)
		0x26, 0xFF, 0x00,  //   Logical Maximum (255)
		0x75, 0x08,        //   Report Size (8)
		0x95, 0x3F,        //   Report Count (63)
		0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
		0xC0,              // End Collection

		// 0x05, 0x01,  // Usage Page (Generic Desktop)
		// 0x09, 0x02,  // Usage (Mouse)
		// 0xA1, 0x01,  // Collection (Application)
		// 0x85, 0x0D,  //   Report ID (12)
		// 0x09, 0x01,  // Usage (Pointer)
		// 0xA1, 0x00,  // Collection (Physical)
		// 0x05, 0x09,  // Usage Page (Button)
		// 0x19, 0x01,  // Usage Minimum (0x01)
		// 0x29, 0x03,  // Usage Maximum (0x03)
		// 0x15, 0x00,  // Logical Minimum (0)
		// 0x25, 0x01,  // Logical Maximum (1)
		// 0x95, 0x03,  // Report Count (3)
		// 0x75, 0x01,  // Report Size (1)
		// 0x81, 0x02,  // Input (Data, Variable, Absolute)
		// 0x95, 0x01,  // Report Count (1)
		// 0x75, 0x05,  // Report Size (5)
		// 0x81, 0x01,  // Input (Constant, Array, Absolute)
		// 0x05, 0x01,  // Usage Page (Generic Desktop)
		// 0x09, 0x30,  // Usage (X)
		// 0x09, 0x31,  // Usage (Y)
		// 0x15, 0x00,  // Logical Minimum (0)
		// 0x26, 0xFF, 0x7F,  // Logical Maximum (32767)
		// 0x35, 0x00,  // Physical Minimum (0)
		// 0x46, 0xFF, 0x7F,  // Physical Maximum (32767)
		// 0x75, 0x10,  // Report Size (16)
		// 0x95, 0x02,  // Report Count (2)
		// 0x81, 0x02,  // Input (Data, Variable, Absolute)
		// 0x05, 0x01,  // Usage Page (Generic Desktop)
		// 0x09, 0x38,  // Usage (Wheel)
		// 0x15, 0x81,  // Logical Minimum (-127)
		// 0x25, 0x7F,  // Logical Maximum (127)
		// 0x35, 0x00,  // Physical Minimum (0)
		// 0x45, 0x00,  // Physical Maximum (0)
		// 0x75, 0x08,  // Report Size (8)
		// 0x95, 0x01,  // Report Count (1)
		// 0x81, 0x06,  // Input (Data, Variable, Relative)
		// 0xC0,  // End Collection
		// 0xC0,  // End Collection
	};

// https://github.com/stephaneAG/usbHidDescriptors
// https://stackoverflow.com/questions/48270478/non-application-top-level-collection

static struct hidg_func_descriptor qisi_touchscreen = {
	.subclass	=	0, // No subclass
	.protocol	= 	0, // 
	.report_length	=	512,
	.report_desc_length	= 743,
	.report_desc = 
	{
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
	},
};

static struct platform_device qisi_hid_touchscreen = {
	.name	=	"hidg",
	.id		=	2,
	.num_resources      	=	0,
	.resource       =	0,
	.dev.platform_data	=	&qisi_touchscreen,
	.dev.release = hid_device_release,
};

//*************************************************************************//

/*-------------------------------------------------------------------------*/

#define HIDG_VENDOR_NUM		0x5153	/* XXX NetChip */
#define HIDG_PRODUCT_NUM	0x0168	/* Linux-USB HID gadget */

/*-------------------------------------------------------------------------*/

struct hidg_func_node {
	struct usb_function_instance *fi;
	struct usb_function *f;
	struct list_head node;
	struct hidg_func_descriptor *func;
};

static LIST_HEAD(hidg_func_list);

/*-------------------------------------------------------------------------*/
USB_GADGET_COMPOSITE_OPTIONS();

static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	/* .bcdUSB = DYNAMIC */

	/* .bDeviceClass =		USB_CLASS_COMM, */
	/* .bDeviceSubClass =	0, */
	/* .bDeviceProtocol =	0, */
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	/* .bMaxPacketSize0 = f(hardware) */

	/* Vendor and product id can be overridden by module parameters.  */
	.idVendor =		cpu_to_le16(HIDG_VENDOR_NUM),
	.idProduct =		cpu_to_le16(HIDG_PRODUCT_NUM),
	.bcdDevice = cpu_to_le16(0x2000),
	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
	/* NO SERIAL NUMBER */
	.bNumConfigurations =	1,
};

static const struct usb_descriptor_header *otg_desc[2];

/* string IDs are assigned dynamically */
static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "AURGA CO., LIMITED",
	[USB_GADGET_PRODUCT_IDX].s = "AURGA Keyboard/Mouse/Touchscreen",
	[USB_GADGET_SERIAL_IDX].s = "",
	{  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};



/****************************** Configurations ******************************/

static int do_config(struct usb_configuration *c)
{
	struct hidg_func_node *e, *n;
	int status = 0;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	list_for_each_entry(e, &hidg_func_list, node) {
		e->f = usb_get_function(e->fi);
		if (IS_ERR(e->f)) {
			status = PTR_ERR(e->f);
			goto put;
		}
		status = usb_add_function(c, e->f);
		if (status < 0) {
			usb_put_function(e->f);
			goto put;
		}
	}

	return 0;
put:
	list_for_each_entry(n, &hidg_func_list, node) {
		if (n == e)
			break;
		usb_remove_function(c, n->f);
		usb_put_function(n->f);
	}
	return status;
}

static struct usb_configuration config_driver = {
	.label			= "HID Gadget",
	.bConfigurationValue	= 1,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes		= USB_CONFIG_ATT_WAKEUP,
	.MaxPower = 0x64,
};

/****************************** Gadget Bind ******************************/

static int hid_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget *gadget = cdev->gadget;
	struct list_head *tmp;
	struct hidg_func_node *n, *m;
	struct f_hid_opts *hid_opts;
	int status, funcs = 0;

	list_for_each(tmp, &hidg_func_list)
		funcs++;

	if (!funcs)
		return -ENODEV;

	list_for_each_entry(n, &hidg_func_list, node) {
		n->fi = usb_get_function_instance("hid");
		if (IS_ERR(n->fi)) {
			status = PTR_ERR(n->fi);
			goto put;
		}
		hid_opts = container_of(n->fi, struct f_hid_opts, func_inst);
		hid_opts->subclass = n->func->subclass;
		hid_opts->protocol = n->func->protocol;
		hid_opts->report_length = n->func->report_length;
		hid_opts->report_desc_length = n->func->report_desc_length;
		hid_opts->report_desc = n->func->report_desc;
	}


	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		goto put;
	device_desc.iManufacturer = USB_GADGET_MANUFACTURER_IDX + 1;
	device_desc.iProduct = USB_GADGET_PRODUCT_IDX + 1;

	if (gadget_is_otg(gadget) && !otg_desc[0]) {
		struct usb_descriptor_header *usb_desc;

		usb_desc = usb_otg_descriptor_alloc(gadget);
		if (!usb_desc) {
			status = -ENOMEM;
			goto put;
		}
		usb_otg_descriptor_init(gadget, usb_desc);
		otg_desc[0] = usb_desc;
		otg_desc[1] = NULL;
	}

	/* register our configuration */
	status = usb_add_config(cdev, &config_driver, do_config);
	if (status < 0)
		goto free_otg_desc;

	coverwrite.bcdDevice = FW_VERSION;
	usb_composite_overwrite_options(cdev, &coverwrite);
	dev_info(&gadget->dev, DRIVER_DESC ", version: " DRIVER_VERSION "\n");
		
	// usb_gadget_connect(cdev->gadget);

	return 0;

free_otg_desc:
	kfree(otg_desc[0]);
	otg_desc[0] = NULL;
put:
	list_for_each_entry(m, &hidg_func_list, node) {
		if (m == n)
			break;
		usb_put_function_instance(m->fi);
	}
	return status;
}

static int hid_unbind(struct usb_composite_dev *cdev)
{
	struct hidg_func_node *n;

	list_for_each_entry(n, &hidg_func_list, node) {
		usb_put_function(n->f);
		usb_put_function_instance(n->fi);
	}

	kfree(otg_desc[0]);
	otg_desc[0] = NULL;

	return 0;
}

static int hidg_plat_driver_probe(struct platform_device *pdev)
{
	struct hidg_func_descriptor *func = dev_get_platdata(&pdev->dev);
	struct hidg_func_node *entry;

	if (!func) {
		dev_err(&pdev->dev, "Platform data missing\n");
		return -ENODEV;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->func = func;
	list_add_tail(&entry->node, &hidg_func_list);

	return 0;
}

static int hidg_plat_driver_remove(struct platform_device *pdev)
{
	struct hidg_func_node *e, *n;
	list_for_each_entry_safe(e, n, &hidg_func_list, node) {
		list_del(&e->node);
		kfree(e);
	}

	return 0;
}


/****************************** Some noise ******************************/


static struct usb_composite_driver hidg_driver = {
	.name		= "g_hid",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_HIGH,
	.bind		= hid_bind,
	.unbind		= hid_unbind,
};

static struct platform_driver hidg_plat_driver = {
	.remove		= hidg_plat_driver_remove,
	.driver		= {
		.name	= "hidg",
	},
};

static struct platform_driver hidg_plat_driver2 = {
	.remove		= hidg_plat_driver_remove,
	.driver		= {
		.name	= "hidp",
	},
};


MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Fabien Chouteau, Peter Korsgaard");
MODULE_LICENSE("GPL");

static int setup_hid(void){
	int status;

	// 
	qisi_touchscreen.report_length = 512;
	qisi_touchscreen.report_desc_length = sizeof(desc_touchscreen);
	memcpy(qisi_touchscreen.report_desc, desc_touchscreen, (int)qisi_touchscreen.report_desc_length);

	platform_device_register(&qisi_hid_keyboard);
	platform_device_register(&qisi_hid_mouse);
	platform_device_register(&qisi_hid_touchscreen);
	platform_device_register(&qisi_hid_mouse2);
	status = platform_driver_probe(&hidg_plat_driver,
				hidg_plat_driver_probe);
	// status = platform_driver_probe(&hidg_plat_driver2,
	// 			hidg_plat_driver_probe);
	if (status < 0)
		return status;

	status = usb_composite_probe(&hidg_driver);
	if (status < 0){
		platform_driver_unregister(&hidg_plat_driver);
		//platform_driver_unregister(&hidg_plat_driver2);
	}
		
	return status;
}

static void uninstall_hid(void){
	platform_device_unregister(&qisi_hid_touchscreen);
	platform_device_unregister(&qisi_hid_mouse);
	platform_device_unregister(&qisi_hid_keyboard);
	platform_device_unregister(&qisi_hid_mouse2);
	platform_driver_unregister(&hidg_plat_driver);
	//platform_driver_unregister(&hidg_plat_driver2);
	usb_composite_unregister(&hidg_driver);

	memset(&qisi_hid_touchscreen.dev, 0, sizeof(qisi_hid_touchscreen.dev));
	qisi_hid_touchscreen.dev.platform_data	=	&qisi_touchscreen;
	qisi_hid_touchscreen.dev.release = hid_device_release;

	memset(&qisi_hid_mouse.dev, 0, sizeof(qisi_hid_mouse.dev));
	qisi_hid_mouse.dev.platform_data	=	&qisi_mouse;
	qisi_hid_mouse.dev.release = hid_device_release;

	memset(&qisi_hid_keyboard.dev, 0, sizeof(qisi_hid_keyboard.dev));
	qisi_hid_keyboard.dev.platform_data	=	&qisi_keyboard;
	qisi_hid_keyboard.dev.release = hid_device_release;

	memset(&qisi_hid_mouse2.dev, 0, sizeof(qisi_hid_mouse2.dev));
	qisi_hid_mouse2.dev.platform_data	=	&qisi_mouse2;
	qisi_hid_mouse2.dev.release = hid_device_release;
}

static void hid_device_release(struct device * dev)
{
	dev->parent = NULL;
}

static void load_hid_fullspeed_task(struct work_struct* task){
	load_aurga_eeprom();
	setup_hid();
	usb_function = 1;
}


static int __init hidg_init(void)
{
	INIT_WORK(&load_nvram_task, load_hid_fullspeed_task);
	schedule_work(&load_nvram_task);
	return 0;
}

static void __exit hidg_cleanup(void)
{
	uninstall_hid();
}

module_init(hidg_init);
module_exit(hidg_cleanup);