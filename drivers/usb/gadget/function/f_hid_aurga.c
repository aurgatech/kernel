#include <linux/sched.h>
#include <linux/mtd/spi-nor.h>
#include <linux/reboot.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/gpio.h>
#include <linux/syscalls.h>
#include <linux/namei.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>

#define REPORT_ID_AURGA 0x0C

#define VEN_CMD_QUERY_STATUS        0xF0
#define VEN_CMD_REBOOT              0xF1


#define VEN_CMD_PREPARE_STAGE1      0xA0
#define VEN_CMD_PREPARE_STAGE2      0xA1
#define VEN_CMD_UPLOAD_FW           0xA2
#define VEN_CMD_END_UPLOAD_FW       0xA3
#define VEN_CMD_UPDATE_FW           0xA4
#define VEN_CMD_GET_UPDATE_PROGRESS           0xA5

#define VEN_CMD_GET_FW_VERSION    0xC2


#define VEN_EC_OK                   0x00
#define VEN_EC_BUFFER_IS_NOT_READY  0x01
#define VEN_EC_FW_HASH_MISMATCH     0x02
#define VEN_EC_FW_NOT_READY         0x03
#define VEN_EC_MTD_MISSING          0x04
#define VEN_EC_MTD_ERROR            0x05
#define VEN_EC_UPDATED              0x4F
#define VEN_EC_TEST_BUSY                           0x83

#define VEN_EC_INSUFFICIENT_MEMORY  0xFF

#define ROM_VERSION 608841728
#define ROM_PART_DATA_OFFSET ((32 + 16)*1024)
#define ROM_PART_DTB_OFFSET ((32 + 16 + 160)*1024)

//  System V Shared Memory
#define IPC_SM_KEY      6253165
#define IPC_SM_LENGTH       (64 * 1024)

#ifdef CONFIG_AURGA_DEBUG_ON
#define _dbgl(fmt, args...)     printk(KERN_WARNING"%s[%d]:" fmt "\n", __func__, __LINE__, ## args)
#define _dbgfl(fmt, args...)    printk(KERN_WARNING"(%s)%s[%d]:" fmt "\n", __FILE__, __func__, __LINE__, ## args)

static void dump_hex(const char* prefix, uint8_t *input, int length){
    char text[2048] = {};
    int i;
    for (i = 0; i < length; i++)
    {
        sprintf(text, "%s %02X", text, input[i]);
    }
    
    _dbgl("%s: %s", prefix, text);
}

#endif

#ifndef _dbgl
#define _dbgl(fmt, args...) 
#endif

#ifndef _dbgfl
#define _dbgfl(fmt, args...) 
#endif

#ifndef dump_hex
#define dump_hex(prefix, input, length)
#endif

struct flash_info_ex {
	char		*name;

	/*
	 * This array stores the ID bytes.
	 * The first three bytes are the JEDIC ID.
	 * JEDEC ID zero means "no ID" (mostly older chips).
	 */
	u8		id[6];
	u8		id_len;

	/* The size listed here is what works with SPINOR_OP_SE, which isn't
	 * necessarily called a "sector" by the vendor.
	 */
	unsigned	sector_size;
	u16		n_sectors;

	u16		page_size;
	u16		addr_width;

	u32		flags;
};

#define ERASE_SIZE (1024*64)

struct system_info{
    u8  chip_type; // Indicate if the chip is S3 or S3L
    u32 flash_size; // Whole size of the nor-spi flash
    char uid[8];
    u8  partition_count; // mtd block count
    struct _partition_info{
        u32 offset;
        u32 size;        
    }partitions[1]; // mtd block info
}__attribute__ ( (packed));

typedef struct{
    u32 offset; // offset to write firmware on flash
    u32 size;   // firmware size
    u8 hash[32]; // sha256 of uploaded firmware
}request_prepare_fw __attribute__ ( (packed)); 

uint8_t device_sn[16] = {};
EXPORT_SYMBOL(device_sn);

static request_prepare_fw requested_fw_info;

static struct work_struct task;
static struct work_struct led_task;
static struct delayed_work dtask;

static u8* qisi_firmware = NULL;
static size_t qisi_firmware_size = 0;
static dma_addr_t qisi_firmware_dma_addr;
static struct f_hidg* g_hidg = NULL;
static int updating_firmware = 0;
static int fw_is_ready = 0;

// Kill all user processes
static int aurga_handle_prepare_stage1(struct usb_request *req){
    struct f_hidg *hidg = (struct f_hidg *)req->context;
    u8* input = (u8 *)req->buf;
    struct task_struct *task;
    
    for_each_process(task) {
        if(strncmp(task->comm, "qs", 2) == 0){
            _dbgl("%s [%d]\n",task->comm , task->pid);
            // SIGTERM/SIGKILL
            kill_pid(get_task_pid(task, PIDTYPE_PID), SIGTERM, 1);
        }
    }

    hidg->set_report_buf[1] = input[1];
    return 0;
}

static void allocate_memory_task(struct work_struct* t){
    int count;
    size_t dma_size = 0;
    uint8_t uid[8] = {};

    count = requested_fw_info.size / ERASE_SIZE;
    if(requested_fw_info.size % ERASE_SIZE)count++;
    dma_size = (size_t)(count * ERASE_SIZE);
    _dbgl("try to allocate %08X", dma_size);
    
    if(qisi_firmware)dma_free_coherent(g_hidg->device, qisi_firmware_size, qisi_firmware, qisi_firmware_dma_addr);
    qisi_firmware = dma_alloc_coherent(g_hidg->device, dma_size, &qisi_firmware_dma_addr, GFP_KERNEL);
    
    if(qisi_firmware){
        _dbgl("memory allocated");
    }else{
        _dbgl("failed to allocate memory");
        //hidg->set_report_buf[2] = VEN_EC_INSUFFICIENT_MEMORY;
    }
    
    qisi_firmware_size = dma_size;
}

static void set_led(int counter){
    int led_red = 139;
    int led_green = 140;

    if(counter == 0){
        gpio_set_value(led_red, 1);
        gpio_set_value(led_green, 1);
    }else if(counter == 1){
        gpio_set_value(led_red, 0);
        gpio_set_value(led_green, 0);
    }else{
        gpio_set_value(led_red, 1);
        gpio_set_value(led_green, 0);
    }
}

static void led_runloop(struct work_struct* t){
    int led_red = 139;
    int led_green = 140;
    int err;
    int counter = 0;
    unsigned long st, et;
   
    err = gpio_request(led_red, "led_red");
    if(err < 0){
        _dbgl("failed to request led red %d", err);
    }else{
        gpio_direction_output(led_red, 0);
    }

    err = gpio_request(led_green, "led_green");
    if(err < 0){
        _dbgl("failed to request led green %d", err);
    }else{
        gpio_direction_output(led_green, 1);
    }

    while(1){
        set_led(counter);
        counter++;
        usleep_range(300000, 300100);

        if(counter > 2){
            counter = 0;
        }
    }

    gpio_free(led_red);
    gpio_free(led_green);
}

static int aurga_handle_prepare_stage2(struct usb_request *req){
    struct f_hidg *hidg = (struct f_hidg *)req->context;
    u8* input = (u8 *)req->buf;
    requested_fw_info = *(request_prepare_fw*)&input[2];
    
    updating_firmware = 1;
    _dbgl("requested_fw_info.offset %d requested_fw_info.size %d", requested_fw_info.offset, requested_fw_info.size);
    _dbgl("hash %016llx", *(u64*)requested_fw_info.hash);
    g_hidg = hidg;
    INIT_WORK(&task, allocate_memory_task);
    schedule_work(&task);
    
    INIT_WORK(&led_task, led_runloop);
    schedule_work(&led_task);
    hidg->set_report_buf[1] = input[1];
    return 0;
}
static u8 system_information[256] = {};
extern int usb_test_mode;
static int aurga_handle_query_status(struct usb_request *req){
    struct f_hidg *hidg = (struct f_hidg *)req->context;
    u8* input = (u8 *)req->buf;
    int i;
    int offset = 0;
    u8 hash[128] = {0};
    struct mtd_info *mtd = NULL, *root_mtd = NULL;
    struct spi_nor* nor = NULL;
    struct system_info* sys_info = (struct system_info*)&hidg->set_report_buf[3];
    unsigned long mem_pages = get_num_physpages();
    mem_pages <<= 2;
    
    if(mem_pages > 70000){
        sys_info->chip_type = 1; // S3 128MB memory
    }else{
        sys_info->chip_type = 0; // S3L 64MB memory
    }

    sys_info->chip_type |= (1 < 4);

    _dbgl("chip type %d memory size %d", sys_info->chip_type, mem_pages);

    memcpy(sys_info->uid, device_sn, 8);

    // Check flash info

    for (i = 0; i < 20; i++)
    {
        mtd = get_mtd_device(NULL, i);
        if (IS_ERR(mtd))break;
        if(!root_mtd)root_mtd = mtd->parent;
    
        sys_info->partitions[i].offset = offset;
        sys_info->partitions[i].size = mtd->size;
        sys_info->partition_count ++;
        offset += mtd->size;

        _dbgl("Parent %px priv %px Index: %d Name: %s Offset: %08X Size: %08X Parent Size %08X", mtd->parent, mtd->parent->priv,
                    mtd->index, mtd->name, sys_info->partitions[i].offset, mtd->size, mtd->parent->size);

        put_mtd_device(mtd);
    }

    if(root_mtd){
        sys_info->flash_size = root_mtd->size;
    }

    memcpy(system_information, sys_info, 64);

    hidg->set_report_buf[1] = input[1];
    
    return 0;
}

static int aurga_handle_upload_fw(struct usb_request *req){
    struct f_hidg *hidg = (struct f_hidg *)req->context;
    u8* input = (u8 *)req->buf;
    u32 offset = *(u32*)&input[2]; 
    
    if(qisi_firmware){
        memcpy(qisi_firmware + offset, &input[6], req->actual - 6);
    }else{
        hidg->set_report_buf[2] = VEN_EC_BUFFER_IS_NOT_READY;
    }
    
    hidg->set_report_buf[1] = input[1];
    return 0;
}

#include <linux/crypto.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>

static int aurga_handle_end_upload_fw(struct usb_request *req)
{
    struct f_hidg *hidg = (struct f_hidg *)req->context;
    u8 *input = (u8 *)req->buf;
    u32 offset = *(u32 *)&input[2];
    u8 hash[SHA256_DIGEST_SIZE]; // Buffer to store the computed hash
    int ret;

    if (qisi_firmware) {
        struct crypto_shash *tfm;
        struct shash_desc *desc;

        // Allocate SHA-256 transform
        tfm = crypto_alloc_shash("sha256", 0, 0);
        if (IS_ERR(tfm)) {
            pr_err("Failed to allocate SHA-256 transform\n");
            return PTR_ERR(tfm);
        }

        // Allocate and initialize descriptor
        desc = kmalloc(sizeof(struct shash_desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
        if (!desc) {
            _dbgl("Failed to allocate descriptor");
            crypto_free_shash(tfm);
            return -ENOMEM;
        }
        desc->tfm = tfm;

        // Initialize the hash operation
        ret = crypto_shash_init(desc);
        if (ret) {
            _dbgl("Failed to initialize SHA-256 hash");
            goto cleanup;
        }

        // Update the hash with the firmware data
        ret = crypto_shash_update(desc, qisi_firmware, requested_fw_info.size);
        if (ret) {
            _dbgl("Failed to update SHA-256 hash");
            goto cleanup;
        }

        // Finalize the hash and store it in the `hash` buffer
        ret = crypto_shash_final(desc, hash);
        if (ret) {
            _dbgl("Failed to finalize SHA-256 hash");
            goto cleanup;
        }

        // Compare the computed hash with the expected hash
        if (memcmp(requested_fw_info.hash, hash, SHA256_DIGEST_SIZE)) {
            _dbgl("Got firmware");
            dump_hex("", hash, SHA256_DIGEST_SIZE);
            _dbgl("Expected");
            dump_hex("", requested_fw_info.hash, SHA256_DIGEST_SIZE);
            hidg->set_report_buf[2] = VEN_EC_FW_HASH_MISMATCH;
        } else {
            _dbgl("Firmware matched!");
            hidg->set_report_buf[2] = VEN_EC_OK;
            fw_is_ready = 1;
        }

cleanup:
        // Free resources
        kfree(desc);
        crypto_free_shash(tfm);
    } else {
        hidg->set_report_buf[2] = VEN_EC_BUFFER_IS_NOT_READY;
        _dbgl("");
    }

    hidg->set_report_buf[1] = input[1];
    return 0;
}

static int get_dtb_offset(void){
    u8 hdr[] = {0xD0, 0x0D, 0xFE, 0xED};
    int i = 0, count = 0;

    for (i = 0; i < qisi_firmware_size - 4; i++)
    {
        if(memcmp(hdr, &qisi_firmware[i], 4) == 0){
            if(i % 0x1000 == 0)return i;
        }
    }
    
    return -1;
}

static void update_firmware_task(struct work_struct* work){
    g_hidg->set_report_buf[0] = REPORT_ID_AURGA;
    g_hidg->set_report_buf[1] = VEN_CMD_GET_UPDATE_PROGRESS;
    g_hidg->set_report_buf[2] = 0;

    u32* progress = (u32*)&g_hidg->set_report_buf[3];
    *progress = 0;
    struct erase_info ei = {};
    int offset, offset_dtb = 0;
    int erase_size, total_size, blocks, write_size;
    int flash_offset = requested_fw_info.offset;
    int rc, i;
    int part = -1;
    struct system_info* sys_info = (struct system_info*)system_information;

    struct mtd_info *mtd = get_mtd_device(NULL, 0);  // bl
    struct mtd_info *mtd1 = get_mtd_device(NULL, 1); // eeprom
    struct mtd_info *mtd2 = get_mtd_device(NULL, 2); // data

    if(IS_ERR(mtd)){
        _dbgl("Could not find mtd device");
        g_hidg->set_report_buf[2] = VEN_EC_MTD_MISSING;
        return;
    }

     _dbgl("MTD0 %08X MTD1 %08X MTD2 %08X", (int)mtd->size, (int)mtd1->size, (int)mtd2->size);
    offset = (int)(mtd->size + mtd1->size);
    offset_dtb = offset + (int)mtd2->size;

    mtd = mtd->parent;
    if(IS_ERR(mtd)){
        _dbgl("Could not find mtd device");
        g_hidg->set_report_buf[2] = VEN_EC_MTD_MISSING;
        return;
    }

    _dbgl("Request %08X Total %08X Offset %08X", requested_fw_info.size, (int)mtd->size, offset);

    erase_size = mtd->erasesize;
    _dbgl("mtd->erasesize %d", erase_size);

    blocks = (requested_fw_info.size + erase_size - 1) / erase_size;
    total_size = blocks * erase_size;
    u8* fw = qisi_firmware;

    if(requested_fw_info.offset == 0 && requested_fw_info.size > offset_dtb && offset == ROM_PART_DATA_OFFSET && offset_dtb == ROM_PART_DTB_OFFSET){
        if(*(u32*)&qisi_firmware[offset - 4] == ROM_VERSION){
            _dbgl("Update kernel & rootfs");
            fw = qisi_firmware + offset_dtb;
            blocks = (requested_fw_info.size - offset_dtb + erase_size - 1) / erase_size;
            total_size = blocks * erase_size;
            flash_offset = offset_dtb;
        }
    }


    if(requested_fw_info.offset == flash_offset){
        offset_dtb = 0;
    }

    _dbgl("writing to flash offset %08X size %d", flash_offset, total_size);
#define MAX_WRITE_BLOCKS    64
#ifndef MIN
#define MIN(a, b)   (a > b ? b : a)
#endif

    for(i = 0; i < sys_info->partition_count; i++){
        _dbgl("part offset %08X part size: %08X req offset %08X", sys_info->partitions[i].offset, sys_info->partitions[i].size, flash_offset);
        if(sys_info->partitions[i].offset == flash_offset){
            part = i;
            _dbgl("Part %d Offset %d Size %d", part, sys_info->partitions[i].offset, sys_info->partitions[i].size);
            break;
        }
    }

    for(i = 0; i < blocks; i+= MAX_WRITE_BLOCKS){
        ei.addr = (uint64_t)(flash_offset + i * erase_size);
	    ei.len  = (uint64_t)(erase_size * MIN(MAX_WRITE_BLOCKS, blocks - i));

        // if(part > 0 && total_size <= sys_info->partitions[part].size && (ei.addr + ei.len) > (sys_info->partitions[part].offset + sys_info->partitions[part].size) ){
        //     _dbgl("expect %d actual %d", (ei.addr + ei.len), (sys_info->partitions[part].offset + sys_info->partitions[part].size));

        //     ei.len = (sys_info->partitions[part].offset + sys_info->partitions[part].size) - ei.addr;
        //     _dbgl("new len %d", ei.len);
        // }

        rc = mtd_erase(mtd, &ei);
        if(rc){
            _dbgl("mtd_erase  %08X", rc);
            g_hidg->set_report_buf[2] = VEN_EC_MTD_ERROR;
            return;
        }
        
        write_size = 0;
        rc = mtd_write(mtd, ei.addr, ei.len, &write_size, fw + (i * erase_size));
        if(rc){
            _dbgl("mtd_write  %08X", rc);
            g_hidg->set_report_buf[2] = VEN_EC_MTD_ERROR;
            return;
        }

        *progress = ((MIN(MAX_WRITE_BLOCKS, blocks - i) + i) * erase_size + offset_dtb);

        //_dbgl("progress %d", *progress);
    }

    g_hidg->set_report_buf[2] = VEN_EC_UPDATED;
    usleep_range(200000, 300000);
    emergency_restart();
    updating_firmware = 0;
}

static void update_firmware_from_file_task(struct work_struct* work){
    struct task_struct *task;
    struct erase_info ei = {};
    int erase_size, total_size, blocks, write_size;
    int rc, i;
    int part = -1;
    int offset = 0, offset_dtb = 0, flash_offset = 0;
    u8* buffer = NULL;
    loff_t pos = 0;
    mm_segment_t old_fs;
    

    char* filename = "/tmp/fw.bin";
    struct kstat stat = {};
    struct path kpath;
    kern_path(filename, 0, &kpath);
    vfs_getattr(&kpath, &stat, STATX_INO, AT_STATX_SYNC_AS_STAT);
    total_size = stat.size;
    _dbgl("Firmware Size: 0x%04X", total_size);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)
    set_fs( old_fs );
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
    set_fs( old_fs );
#else
    force_uaccess_end(old_fs);
#endif

    struct file * f = filp_open(filename, O_RDONLY, 0);
    if(f == NULL){
        _dbgl("failed to open fw file!!");
        return;
    }

    if(IS_ERR(f)){
        filp_close(f, NULL);
        _dbgl("");
        return;
    }




    if(total_size == 0)return;
    
    for_each_process(task) {
        if(strncmp(task->comm, "qs", 2) == 0){
            _dbgl("%s [%d]\n",task->comm , task->pid);
            // SIGTERM/SIGKILL
            kill_pid(get_task_pid(task, PIDTYPE_PID), SIGTERM, 1);
        }
    }

    usleep_range(500000, 1000000);

    INIT_WORK(&led_task, led_runloop);
    schedule_work(&led_task);

    struct mtd_info *mtd = get_mtd_device(NULL, 0); // bl
    struct mtd_info *mtd1 = get_mtd_device(NULL, 1); // eeprom
    struct mtd_info *mtd2 = get_mtd_device(NULL, 2); // data
    if(IS_ERR(mtd)){
        _dbgl("Could not find mtd device");
        g_hidg->set_report_buf[2] = VEN_EC_MTD_MISSING;
        return;
    }

    offset = (int)(mtd->size + mtd1->size);
    offset_dtb = offset + (int)mtd2->size;

    mtd = mtd->parent;
    if(IS_ERR(mtd)){
        _dbgl("Could not find mtd device");
        g_hidg->set_report_buf[2] = VEN_EC_MTD_MISSING;
        return;
    }

#define MAX_WRITE_BLOCKS    64
#ifndef MIN
#define MIN(a, b)   (a > b ? b : a)
#endif

    _dbgl("offset %08X offset_dtb %08X", offset, offset_dtb);
    erase_size = mtd->erasesize;
    buffer = kzalloc((erase_size * MAX_WRITE_BLOCKS), GFP_ATOMIC);

    //_dbgl("mtd->erasesize %d", erase_size);
    blocks = total_size / erase_size;
    // Check ROM version
    //f->f_pos = (off_t)(offset - erase_size);
    pos = (off_t)(offset - erase_size);
    //vfs_setpos(f, (off_t)(offset - erase_size), erase_size);
    kernel_read(f, buffer, erase_size, &pos);

    _dbgl("Info erase size %d total size%d data %08X dtb %08X pos %08X version %08X", erase_size, total_size, offset, offset_dtb, (int)pos, *(u32*)&buffer[erase_size - 4]);

    // Reset read offset
    //f->f_pos = 0;
    pos = 0;
    if(*(u32*)&buffer[erase_size - 4] == ROM_VERSION && offset == ROM_PART_DATA_OFFSET && offset_dtb == ROM_PART_DTB_OFFSET){
        _dbgl("Update kernel & rootfs");
        blocks = offset_dtb / erase_size;
        // Read out the first part
        for(i = 0; i < blocks; i++){
            kernel_read(f, buffer, erase_size, &pos);
        }
        
        blocks = (total_size - offset_dtb + erase_size - 1) / erase_size;
        flash_offset = offset_dtb;

        // Set read offset from dtb partition
        pos = (off_t)offset_dtb;
    }

    for(i = 0; i < blocks; i+= MAX_WRITE_BLOCKS){
        ei.addr = (uint64_t)(i * erase_size + flash_offset);
	    ei.len  = (uint64_t)(erase_size * MIN(MAX_WRITE_BLOCKS, blocks - i));
        kernel_read(f, buffer, ei.len, &pos);
        if(i == 0){
            _dbgl("header %04X", *(u32*)buffer);
        }

        rc = mtd_erase(mtd, &ei);
        if(rc){
            _dbgl("mtd_erase  %08X", rc);
            return;
        }
        
        write_size = 0;
        rc = mtd_write(mtd, ei.addr, ei.len, &write_size, buffer);
        if(rc){
            _dbgl("mtd_write  %08X", rc);
            return;
        }
    }

    kfree(buffer);

    usleep_range(200000, 300000);
    emergency_restart();
    updating_firmware = 0;
}

void update_firmware_from_file(void){
    if(updating_firmware)return;
    updating_firmware = 1;

    INIT_WORK(&task, update_firmware_from_file_task);
    schedule_work(&task);
}

EXPORT_SYMBOL(update_firmware_from_file);

static int aurga_handle_update_fw(struct usb_request *req){
    struct f_hidg *hidg = (struct f_hidg *)req->context;
    u8* input = (u8 *)req->buf;
    
    if(qisi_firmware && requested_fw_info.size && fw_is_ready){
        INIT_WORK(&task, update_firmware_task);
        schedule_work(&task);
    }else{
        hidg->set_report_buf[2] = VEN_EC_FW_NOT_READY;
        _dbgl("");
    }

    hidg->set_report_buf[1] = input[1];
    return 0;
}

static int status_loaded = 0;
static char build_version[64] = {};

static int aurga_set_report(struct usb_request *req){
    struct f_hidg *hidg = (struct f_hidg *)req->context;
    u8* input = (u8 *)req->buf;
    u8 cmd = input[1];
 
    memset(hidg->set_report_buf, 0, hidg->set_report_length);
    hidg->set_report_buf[0] = REPORT_ID_AURGA;


    if(cmd == VEN_CMD_QUERY_STATUS){
        aurga_handle_query_status(req);
    }

    if(cmd == VEN_CMD_PREPARE_STAGE1){
        aurga_handle_prepare_stage1(req);
    }

    if(cmd == VEN_CMD_PREPARE_STAGE2){
        aurga_handle_prepare_stage2(req);
    }

    if(cmd == VEN_CMD_UPLOAD_FW){
        aurga_handle_upload_fw(req);
    }

    if(cmd == VEN_CMD_END_UPLOAD_FW){
        aurga_handle_end_upload_fw(req);
    }

    if(cmd == VEN_CMD_UPDATE_FW){
        aurga_handle_update_fw(req);
    }

    if(cmd == VEN_CMD_REBOOT){
        emergency_restart();
    }

    return 0;
}

static int aurga_get_report(struct usb_request		*req,
    struct f_hidg			*hidg
){
    if(hidg->set_report_buf){
        memcpy(req->buf, hidg->set_report_buf, req->actual);
    }

    return 0;
}

static int aurga_get_touch_05(struct usb_request		*req,
    struct f_hidg			*hidg
){
    u8 repData[] = {
                0x05,
                0xfc, 0x28, 0xfe, 0x84, 0x40, 0xcb, 0x9a, 0x87, 0x0d, 0xbe, 0x57, 0x3c, 0xb6, 0x70, 0x09, 0x88,
				0x07, 0x97, 0x2d, 0x2b, 0xe3, 0x38, 0x34, 0xb6, 0x6c, 0xed, 0xb0, 0xf7, 0xe5, 0x9c, 0xf6, 0xc2,
				0x2e, 0x84, 0x1b, 0xe8, 0xb4, 0x51, 0x78, 0x43, 0x1f, 0x28, 0x4b, 0x7c, 0x2d, 0x53, 0xaf, 0xfc,
				0x47, 0x70, 0x1b, 0x59, 0x6f, 0x74, 0x43, 0xc4, 0xf3, 0x47, 0x18, 0x53, 0x1a, 0xa2, 0xa1, 0x71,
				0xc7, 0x95, 0x0e, 0x31, 0x55, 0x21, 0xd3, 0xb5, 0x1e, 0xe9, 0x0c, 0xba, 0xec, 0xb8, 0x89, 0x19,
				0x3e, 0xb3, 0xaf, 0x75, 0x81, 0x9d, 0x53, 0xb9, 0x41, 0x57, 0xf4, 0x6d, 0x39, 0x25, 0x29, 0x7c,
				0x87, 0xd9, 0xb4, 0x98, 0x45, 0x7d, 0xa7, 0x26, 0x9c, 0x65, 0x3b, 0x85, 0x68, 0x89, 0xd7, 0x3b,
				0xbd, 0xff, 0x14, 0x67, 0xf2, 0x2b, 0xf0, 0x2a, 0x41, 0x54, 0xf0, 0xfd, 0x2c, 0x66, 0x7c, 0xf8,
				0xc0, 0x8f, 0x33, 0x13, 0x03, 0xf1, 0xd3, 0xc1, 0x0b, 0x89, 0xd9, 0x1b, 0x62, 0xcd, 0x51, 0xb7,
				0x80, 0xb8, 0xaf, 0x3a, 0x10, 0xc1, 0x8a, 0x5b, 0xe8, 0x8a, 0x56, 0xf0, 0x8c, 0xaa, 0xfa, 0x35,
				0xe9, 0x42, 0xc4, 0xd8, 0x55, 0xc3, 0x38, 0xcc, 0x2b, 0x53, 0x5c, 0x69, 0x52, 0xd5, 0xc8, 0x73,
				0x02, 0x38, 0x7c, 0x73, 0xb6, 0x41, 0xe7, 0xff, 0x05, 0xd8, 0x2b, 0x79, 0x9a, 0xe2, 0x34, 0x60,
				0x8f, 0xa3, 0x32, 0x1f, 0x09, 0x78, 0x62, 0xbc, 0x80, 0xe3, 0x0f, 0xbd, 0x65, 0x20, 0x08, 0x13,
				0xc1, 0xe2, 0xee, 0x53, 0x2d, 0x86, 0x7e, 0xa7, 0x5a, 0xc5, 0xd3, 0x7d, 0x98, 0xbe, 0x31, 0x48,
				0x1f, 0xfb, 0xda, 0xaf, 0xa2, 0xa8, 0x6a, 0x89, 0xd6, 0xbf, 0xf2, 0xd3, 0x32, 0x2a, 0x9a, 0xe4,
				0xcf, 0x17, 0xb7, 0xb8, 0xf4, 0xe1, 0x33, 0x08, 0x24, 0x8b, 0xc4, 0x43, 0xa5, 0xe5, 0x24, 0xc2
				};

    int length = sizeof(repData);
    memcpy(req->buf, repData, length);
    if(hidg->report_length < length)hidg->report_length = length;
    return 0;
}

static int aurga_get_touch_06(struct usb_request		*req,
    struct f_hidg			*hidg
){
    u8 repData[] = {0x06, 0x0a, 0x3e, 0x50, 0x92, 0xb2, 0xe9, 0x9a, 0x29, 0x81, 0xd6, 0x43, 0xd1, 0x73, 0x5c, 0x27, 0x1d, 0xd9, 0x29, 0xd1, 0x68, 0xc2, 0x89, 0x4e, 0xb8, 0xbd, 0x9c, 0x9e, 0xc6, 0x6b, 0x55, 0xbb
			, 0xe7, 0x16, 0xd4, 0x25, 0xa0, 0xff, 0x50, 0x96, 0x4a, 0xbb, 0x7d, 0x34, 0xbe, 0x9a, 0x32, 0x85, 0xa2, 0x15, 0x46, 0x56, 0xa4, 0x38, 0x2b, 0x2a, 0xd8, 0x70, 0xcc, 0x36, 0xf3, 0x4b, 0x00, 0x03
			, 0xff, 0xac, 0x12, 0xcb, 0x31, 0x71, 0x5c, 0x0b, 0x1d, 0x8d, 0x92, 0x1c, 0xa1, 0x03, 0x61, 0x34, 0x94, 0x45, 0x3e, 0x2f, 0x09, 0x09, 0xb3, 0x94, 0xb6, 0x60, 0x9b, 0x57, 0x5d, 0xff, 0x2b, 0x2d
			, 0x09, 0x58, 0x53, 0x8f, 0xcc, 0xb2, 0xa1, 0xcf, 0x38, 0x51, 0x72, 0x5a, 0x67, 0xa8, 0xc0, 0x72, 0x75, 0x41, 0x99, 0xac, 0xf4, 0xa0, 0x67, 0xf9, 0x33, 0x64, 0xd8, 0xd4, 0xf3, 0xd3, 0xa9, 0xb6
			, 0xff, 0xbd, 0x73, 0x52, 0x59, 0xfb, 0xa7, 0x49, 0x50, 0x4a, 0x22, 0x30, 0x8e, 0x22, 0xa2, 0x88, 0x5a, 0x18, 0x11, 0xa2, 0xd7, 0xfb, 0x42, 0x2a, 0x60, 0xf1, 0x79, 0x0c, 0xfd, 0x18, 0x15, 0x19
			, 0x23, 0x60, 0xe2, 0x44, 0x40, 0x9f, 0x0f, 0xc6, 0x80, 0x16, 0x5a, 0xe9, 0x96, 0xdd, 0xdb, 0x4a, 0xfb, 0x26, 0xa5, 0x55, 0xb4, 0x9f, 0x41, 0x9a, 0xbe, 0xfd, 0xce, 0x0a, 0xb2, 0xbd, 0x1a, 0x9a
			, 0x15, 0x5e, 0x44, 0x1d, 0xde, 0x92, 0xba, 0x2c, 0xd1, 0xcc, 0x0a, 0x5c, 0x36, 0x2a, 0x0d, 0x90, 0x81, 0x23, 0xde, 0x38, 0x43, 0x71, 0x12, 0xca, 0x4d, 0x7b, 0xae, 0x81, 0x2f, 0x09, 0xf1, 0x07
			, 0x01, 0x3d, 0xd7, 0x01, 0x87, 0x42, 0x6d, 0x4a, 0x1f, 0xde, 0x74, 0xb3, 0x1f, 0x9b, 0x41, 0xdd, 0xcc, 0x56, 0xf4, 0x07, 0xa3, 0x07, 0x03, 0x14, 0x59, 0x47, 0x5f, 0x89, 0xfa, 0xef, 0xbc, 0xf7
			, 0xad, 0x3d};

    int length = sizeof(repData);
    memcpy(req->buf, repData, length);
    if(hidg->report_length < length)hidg->report_length = length;
    return 0;
}