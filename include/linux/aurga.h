#ifndef _AURGA_H_
#define _AURGA_H_

#define AURGA_MISC_DRIVER_MAJOR_NUM 133

#define IOCTL_AURGA_MISC_UPDATE_FIRMWARE						_IOR(AURGA_MISC_DRIVER_MAJOR_NUM, 0, char *)
#define IOCTL_AURGA_MISC_GET_HID_SPEED							_IOR(AURGA_MISC_DRIVER_MAJOR_NUM, 1, char *)
#define IOCTL_AURGA_MISC_SET_HID_SPEED							_IOR(AURGA_MISC_DRIVER_MAJOR_NUM, 2, char *)
#define IOCTL_AURGA_MISC_GET_RESOLUTION							_IOR(AURGA_MISC_DRIVER_MAJOR_NUM, 3, char *)
#define IOCTL_AURGA_MISC_SET_RESOLUTION							_IOR(AURGA_MISC_DRIVER_MAJOR_NUM, 4, char *)
#define IOCTL_AURGA_MISC_BIND_DEVICE						    _IOR(AURGA_MISC_DRIVER_MAJOR_NUM, 5, char *)
#define IOCTL_AURGA_MISC_UNBIND_DEVICE						    _IOR(AURGA_MISC_DRIVER_MAJOR_NUM, 6, char *)
#define IOCTL_AURGA_MISC_GET_DEVICE_SN						    _IOR(AURGA_MISC_DRIVER_MAJOR_NUM, 7, char *)
#define IOCTL_AURGA_MISC_GET_EEPROM						    	_IOR(AURGA_MISC_DRIVER_MAJOR_NUM, 80, char *)
#define IOCTL_AURGA_MISC_RESET_EEPROM						    _IOR(AURGA_MISC_DRIVER_MAJOR_NUM, 81, char *)

#pragma pack(1)
// Total 512 bytes
typedef struct _AURGA_EDID_INFO
{
	unsigned int primary_res_width;
	unsigned int primary_res_height;
	unsigned char edid_used;
	unsigned char edid[256];
	unsigned char reserved[247];
}AURGA_EDID_INFO;

// Total 64 bytes
typedef struct _AURGA_USB_INFO{
	unsigned char hid_2_0;
	unsigned int reserved[15];
}AURGA_USB_INFO;

// Total 256 bytes
typedef struct _AURGA_ACCOUNT_INFO_V1{
	unsigned char device_binded;
	unsigned char binded_uid[16];
	unsigned char reserved[111];
}AURGA_ACCOUNT_INFO_V1;

// Total 440 bytes
typedef struct _AURGA_ACCOUNT_INFO{
	unsigned char device_binded;
	unsigned char binded_uid[16];
	unsigned char custom_cloud_server;
	char cloud_server[422];
}AURGA_ACCOUNT_INFO;

#define AURGA_EEPROM_MAGIC 0x52484541
typedef struct _AURGA_EEPROM_V1{
	unsigned int magic;
	unsigned int version;
	AURGA_ACCOUNT_INFO_V1 account_info;
	AURGA_USB_INFO usb_info;
	AURGA_EDID_INFO edid_info;
}AURGA_EEPROM_V1;

typedef struct _AURGA_EEPROM{
	unsigned int magic;
	unsigned int version;
	AURGA_USB_INFO usb_info;
	AURGA_EDID_INFO edid_info;
	AURGA_ACCOUNT_INFO account_info;
}AURGA_EEPROM;

#pragma pack()

#ifdef __KERNEL__
extern AURGA_EEPROM aurga_eeprom;

#ifdef CONFIG_AURGA_DEBUG_ON
#define DEV_WARN(fmt, args...) pr_warn("%s[%d](%s):" fmt "\n", __FILE__, __LINE__, __func__, ## args)
#else
#define DEV_WARN(fmt, args...)
#endif

#define DEV_ERR(fmt, args...) pr_err("%s[%d](%s):" fmt "\n", __FILE__, __LINE__, __func__, ## args)

void load_aurga_eeprom(void);
void save_aurga_eeprom(int restart);
#endif

#endif