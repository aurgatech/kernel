#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/aurga.h>

extern void update_firmware_from_file(void);
extern uint8_t device_sn[];

static long int aurga_misc_driver_device_ioctl(
    struct file *file,
    unsigned int cmd,/* The number of the ioctl */
    unsigned long user) /* The parameter to it */
{
	int r = 0;
	int mode = 0;
	unsigned int val = 0;
	uint8_t buf[436] = {};

	switch (cmd) {
      case IOCTL_AURGA_MISC_GET_DEVICE_SN:
        if(*(u32*)device_sn > 0 && *(u32*)&device_sn[4] > 0 && copy_to_user((unsigned long *)user, device_sn, 8) == 0){
            r = 8;
        }
        break;
      case IOCTL_AURGA_MISC_UPDATE_FIRMWARE:
        DEV_WARN("Update firmware");
        update_firmware_from_file();
		break;
	  case IOCTL_AURGA_MISC_GET_HID_SPEED:
        val = (unsigned int)aurga_eeprom.usb_info.hid_2_0;
	  	if(copy_to_user((unsigned long *)user, &val, sizeof(val)) == 0){
			DEV_WARN("get hid speed %d", val);
		}
		break;
	  case IOCTL_AURGA_MISC_GET_EEPROM:
	  	if(copy_to_user((unsigned long *)user, &aurga_eeprom, sizeof(AURGA_EEPROM)) == 0){
			DEV_WARN("get eeprom");
			r = sizeof(AURGA_EEPROM);
		}
		break;
	  case IOCTL_AURGA_MISC_SET_HID_SPEED:
		if(copy_from_user(&mode, (unsigned long *)user, 4) == 0){
            if(mode){
                aurga_eeprom.usb_info.hid_2_0 = 0x1;
            }else{
                aurga_eeprom.usb_info.hid_2_0 = 0x0;
            }

            save_aurga_eeprom(1);
        }
		break;
	  case IOCTL_AURGA_MISC_GET_RESOLUTION:
	  	*(unsigned short *)&val = (unsigned short)aurga_eeprom.edid_info.primary_res_width;
		((unsigned short *)&val)[1] = (unsigned short)aurga_eeprom.edid_info.primary_res_width;
	  	if(copy_to_user((unsigned long *)user, &val, sizeof(val)) == 0){
			DEV_WARN("get resolution %d x %d", aurga_eeprom.edid_info.primary_res_width, aurga_eeprom.edid_info.primary_res_height);
		}
		break;
	  case IOCTL_AURGA_MISC_SET_RESOLUTION:
		if(copy_from_user(&val, (unsigned long *)user, 4) == 0){
			aurga_eeprom.edid_info.primary_res_width = ((unsigned short*)&val)[0];
			aurga_eeprom.edid_info.primary_res_height = ((unsigned short*)&val)[1];
			DEV_WARN("set resolution %d x %d", aurga_eeprom.edid_info.primary_res_width, aurga_eeprom.edid_info.primary_res_height);
			save_aurga_eeprom(1);
		}
		break;
	  case IOCTL_AURGA_MISC_BIND_DEVICE:
	  	DEV_WARN("IOCTL_AURGA_MISC_BIND_DEVICE");
		if(copy_from_user(buf, (unsigned long *)user, 436) == 0){
			aurga_eeprom.account_info.device_binded = 1;
			memcpy(aurga_eeprom.account_info.binded_uid, buf, 16);
			memcpy(aurga_eeprom.account_info.cloud_server, &buf[16], 420);
			aurga_eeprom.account_info.custom_cloud_server = (strlen(aurga_eeprom.account_info.cloud_server) > 0);
			DEV_WARN("bind device, custom server: %s", aurga_eeprom.account_info.cloud_server);
			save_aurga_eeprom(0);
		}
		break;
	  case IOCTL_AURGA_MISC_UNBIND_DEVICE:
			aurga_eeprom.account_info.device_binded = 0;
			memset(aurga_eeprom.account_info.binded_uid, 0, sizeof(aurga_eeprom.account_info.binded_uid));
			DEV_WARN("unbind device");
			save_aurga_eeprom(0);
		break;
	  case IOCTL_AURGA_MISC_RESET_EEPROM:
			memset(&aurga_eeprom, 0, sizeof(aurga_eeprom));
			DEV_WARN("reset eeprom");
			save_aurga_eeprom(0);
		break;
	  default:
			DEV_WARN("Unknown IOCTL %d", cmd);
		break;
	}
	
	return r;
}

static struct file_operations aurga_misc_driver_fops = {
  .owner = THIS_MODULE,
  .unlocked_ioctl = aurga_misc_driver_device_ioctl,   /* ioctl */
};

static struct miscdevice aurga_misc_driver_dev = {
	.minor = AURGA_MISC_DRIVER_MAJOR_NUM,
	.name = "aurga_misc",
	.fops = &aurga_misc_driver_fops
};

static int __init aurga_misc_driver_init(void)
{
	printk(KERN_WARNING"AURGA Misc Driver registered!\n");
	misc_register(&aurga_misc_driver_dev);
	return 0;
}

static void __exit aurga_misc_driver_cleanup(void)
{
	misc_deregister(&aurga_misc_driver_dev);
}

module_init(aurga_misc_driver_init);
module_exit(aurga_misc_driver_cleanup);