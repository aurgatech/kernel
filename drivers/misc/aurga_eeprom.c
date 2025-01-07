#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/ioctl.h>
#include <linux/mtd/spi-nor.h>
#include <linux/sched.h>
#include <linux/reboot.h>
#include <linux/aurga.h>

#define EEPROM_VERSION 0x03

#define ROM_PART_EEPROM_OFFSET (32 * 1024)

AURGA_EEPROM aurga_eeprom = {};
EXPORT_SYMBOL (aurga_eeprom);

static int need_reboot = 0;

static void dump_hex(const char* prefix, uint8_t *input, int length){
	int size = length * 3 + 256;
    char* text = kmalloc(size, GFP_ATOMIC);
	memset(text, 0, size);

    int i;
    for (i = 0; i < length; i++)
    {
		if(i % 16 == 0){
			sprintf(text, "%s\n", text);
		}
        sprintf(text, "%s %02X", text, input[i]);
    }
    
    pr_warn("%s: size %d\n%s\n", prefix, length, text);
	kfree(text);
}

void load_aurga_eeprom(void){
    struct mtd_info *mtd_bl = NULL, *mtd_eeprom = NULL;
	int outsize = 0;
	AURGA_EEPROM* eeprom;
	AURGA_EEPROM_V1* v1;
	u8* buf = NULL;
	aurga_eeprom.version = EEPROM_VERSION;
	aurga_eeprom.magic = AURGA_EEPROM_MAGIC;
	// max spi flash erase block size

	mtd_bl = get_mtd_device(NULL, 0);

	if(IS_ERR_OR_NULL(mtd_bl)){
		DEV_WARN("could not find bl mtd!!!\n");
		goto END;
	}

	if(mtd_bl->size != ROM_PART_EEPROM_OFFSET){
		DEV_WARN("bad eeprom offset!!! %08llX expected %08X", mtd_bl->size, ROM_PART_EEPROM_OFFSET);
		goto END;
	}

	mtd_eeprom = get_mtd_device(NULL, 1);
	if(IS_ERR_OR_NULL(mtd_eeprom)){
		DEV_WARN("could not find eeprom mtd!!!\n");
		goto END;
	}

	buf = kzalloc(0x10000, GFP_ATOMIC);

	DEV_WARN("");

	mtd_lock(mtd_eeprom, 0, mtd_eeprom->size);
	mtd_read(mtd_eeprom, 0, sizeof(aurga_eeprom), &outsize, buf);
	mtd_unlock(mtd_eeprom, 0, mtd_eeprom->size);
    DEV_WARN("");

	eeprom = (AURGA_EEPROM*)buf;
	if(eeprom->magic != AURGA_EEPROM_MAGIC){
		DEV_WARN("bad eeprom magic!!! %08X", eeprom->magic);
		goto END;
	}
	
	//dump_hex("eeprom", buf, sizeof(aurga_eeprom));
	if (eeprom->version < EEPROM_VERSION)
	{
		// upgrade eeprom
		DEV_WARN("upgrade eeprom from %d to %d", eeprom->version, EEPROM_VERSION);
		if(eeprom->version == 2){
			v1 = (AURGA_EEPROM_V1*)buf;
			aurga_eeprom.usb_info = v1->usb_info;
			aurga_eeprom.edid_info = v1->edid_info;
			aurga_eeprom.account_info.device_binded = v1->account_info.device_binded;
			memcpy(aurga_eeprom.account_info.binded_uid, v1->account_info.binded_uid, 16);
		}
	}else{
		memcpy(&aurga_eeprom, eeprom, sizeof(aurga_eeprom));
	}
	
	
    DEV_WARN("");
END:
	if(!IS_ERR(mtd_bl))put_mtd_device(mtd_bl);
	if(!IS_ERR(mtd_eeprom))put_mtd_device(mtd_eeprom);
	if(buf)kfree(buf);
}

EXPORT_SYMBOL (load_aurga_eeprom);

static void set_nvram(struct work_struct* task){
	struct mtd_info *mtd_bl = NULL, *mtd_eeprom = NULL;
	struct erase_info ei = {};
	int r = 0;
	int outsize = 0;
	u8* buf = NULL;

	mtd_bl = get_mtd_device(NULL, 0);

	if(IS_ERR_OR_NULL(mtd_bl)){
		DEV_WARN("could not find bl mtd!!!\n");
		goto END;
	}

	if(mtd_bl->size != ROM_PART_EEPROM_OFFSET){
		DEV_WARN("bad eeprom offset!!! %08llX", mtd_bl->size);
		goto END;
	}

	mtd_eeprom = get_mtd_device(NULL, 1);
	if(IS_ERR_OR_NULL(mtd_eeprom)){
		DEV_WARN("could not find eeprom mtd!!!\n");
		goto END;
	}

	buf = kzalloc(0x10000, GFP_ATOMIC);

	// copy eeprom to buffer	
	memcpy(buf, &aurga_eeprom, sizeof(aurga_eeprom));
	DEV_WARN("Erase Size: %08X", mtd_eeprom->erasesize);
	//dump_hex("eeprom", buf, sizeof(aurga_eeprom));
	mtd_lock(mtd_eeprom, 0, mtd_eeprom->size);

	ei.addr = 0;
	ei.len  = (u64)mtd_eeprom->erasesize;
	//_dbgl("Addr: %llu - %llu", ei.addr, ei.len);
	//_dbgl("Addr: %llx - %llx", ei.addr, ei.len);
	r = mtd_erase(mtd_eeprom, &ei);
	if(r){
		DEV_WARN(" failed to erase eeprom %d", r);
	}else{
		r = mtd_write(mtd_eeprom, ei.addr, ei.len, &outsize, buf);
		if(r){
			DEV_WARN("failed to write eeprom");
		}
	}
	mtd_unlock(mtd_eeprom, 0, mtd_eeprom->size);
	
	if(need_reboot){
		usleep_range(500000, 800000);
		emergency_restart();
	}	
	// uninstall_hid();
	// usleep_range(1000000, 1100000);
	// setup_hid();
END:
	if(!IS_ERR(mtd_bl))put_mtd_device(mtd_bl);
	if(!IS_ERR(mtd_eeprom))put_mtd_device(mtd_eeprom);
	DEV_WARN("");
	if(buf)kfree(buf);
}

static struct delayed_work set_nvram_task;

void save_aurga_eeprom(int restart){
	need_reboot = restart;
	INIT_DELAYED_WORK(&set_nvram_task, set_nvram);
	schedule_delayed_work(&set_nvram_task, 1 * HZ);
}

EXPORT_SYMBOL (save_aurga_eeprom);