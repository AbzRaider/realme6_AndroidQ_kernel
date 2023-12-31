/***************************************************
 * File:firmware.c
 * VENDOR_EDIT
 * Copyright(C) 2008-2012 OPPO Mobile Comm Corp., Ltd
 * Description:
 *             ili9881 driver
 * Version:2.0:
 * Date created:2019/04/09
 * TAG: BSP.TP.Init
 * *
 * -------------- Revision History: -----------------
   <author >  <data>  <version>  <desc>
****************************************************/

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/fd.h>
#include <linux/file.h>
#include <linux/version.h>
#include <asm/uaccess.h>

#include "ilitek_common.h"
#include "firmware.h"
#include "protocol.h"

struct flash_sector {
    uint32_t ss_addr;
    uint32_t se_addr;
    uint32_t checksum;
    uint32_t crc32;
    uint32_t dlength;
    bool data_flag;
    bool inside_block;
};

struct flash_block_info {
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t hex_crc;
    uint32_t block_crc;
};

__attribute__((weak)) void set_tp_fw_done(void) {printk("set_tp_fw_done() in tp\n");}

#ifdef HOST_DOWNLOAD
static unsigned char CTPM_FW[MAX_CTPM_FW_LEN] = {0};
#endif

/*
 * the size of two arrays is different depending on
 * which of methods to upgrade firmware you choose for.
 */
static uint8_t *flash_fw = NULL;

#ifdef HOST_DOWNLOAD
static uint8_t ap_fw[MAX_AP_FIRMWARE_SIZE] = { 0 };
static uint8_t dlm_fw[MAX_DLM_FIRMWARE_SIZE] = { 0 };
static uint8_t mp_fw[MAX_MP_FIRMWARE_SIZE] = { 0 };
static uint8_t gesture_fw[MAX_GESTURE_FIRMWARE_SIZE] = { 0 };
#endif

static struct flash_sector *g_flash_sector = NULL;
static struct flash_block_info g_flash_block_info[4];

struct core_firmware_data *core_firmware = NULL;

static uint32_t HexToDec(char *pHex, int32_t nLength)
{
    uint32_t nRetVal = 0;
    uint32_t nTemp = 0;
    uint32_t i = 0;
    int32_t nShift = (nLength - 1) * 4;

    for (i = 0; i < nLength; nShift -= 4, i++) {
        if ((pHex[i] >= '0') && (pHex[i] <= '9')) {
            nTemp = pHex[i] - '0';
        } else if ((pHex[i] >= 'a') && (pHex[i] <= 'f')) {
            nTemp = (pHex[i] - 'a') + 10;
        } else if ((pHex[i] >= 'A') && (pHex[i] <= 'F')) {
            nTemp = (pHex[i] - 'A') + 10;
        } else {
            return -1;
        }

        nRetVal |= (nTemp << nShift);
    }

    return nRetVal;
}

static uint32_t calc_crc32(uint32_t start_addr, uint32_t end_addr, uint8_t *data)
{
    int i = 0;
    int j = 0;
    uint32_t CRC_POLY = 0x04C11DB7;
    uint32_t ReturnCRC = 0xFFFFFFFF;
    uint32_t len = start_addr + end_addr;

    for (i = start_addr; i < len; i++) {
        ReturnCRC ^= (data[i] << 24);

        for (j = 0; j < 8; j++) {
            if ((ReturnCRC & 0x80000000) != 0) {
                ReturnCRC = ReturnCRC << 1 ^ CRC_POLY;
            } else {
                ReturnCRC = ReturnCRC << 1;
            }
        }
    }

    return ReturnCRC;
}

#ifdef HOST_DOWNLOAD
static int check_hex_crc(void)
{
    int ap_crc = 0;
    int dlm_crc = 0;
    int mp_crc = 0;
    int gesture_crc = 0;

    int hex_ap_crc = 0;
    int hex_dlm_crc = 0;
    int hex_mp_crc = 0;
    int hex_gesture_crc = 0;
    bool check_fail = false;

    int addr_len = 0;

    addr_len = g_flash_block_info[0].end_addr - g_flash_block_info[0].start_addr;
    if ((addr_len >= 4) && (addr_len < MAX_AP_FIRMWARE_SIZE)) {
        ap_crc = calc_crc32(0, addr_len - 3, ap_fw);
        hex_ap_crc = ((ap_fw[addr_len - 3] << 24) + (ap_fw[addr_len - 2] << 16) + \
            (ap_fw[addr_len - 1] << 8) + ap_fw[addr_len]);
    }
    else {
        check_fail = true;
        TPD_INFO("get ap addr len error addr_len = 0x%X\n", addr_len);
    }
    TPD_DEBUG("ap block, driver crc = 0x%X, hex ap crc = 0x%X\n", ap_crc, hex_ap_crc);

    addr_len = g_flash_block_info[1].end_addr - g_flash_block_info[1].start_addr;
    if ((addr_len >= 4) && (addr_len < MAX_DLM_FIRMWARE_SIZE)) {
        dlm_crc = calc_crc32(0, addr_len -3, dlm_fw);
        hex_dlm_crc = ((dlm_fw[addr_len - 3] << 24) + (dlm_fw[addr_len - 2] << 16) + \
            (dlm_fw[addr_len - 1] << 8) + dlm_fw[addr_len]);
    }
    else {
        check_fail = true;
        TPD_INFO("get dlm addr len error addr_len = 0x%X\n", addr_len);
    }
    TPD_DEBUG("dlm block, driver crc = 0x%X, hex dlm crc = 0x%X\n", dlm_crc, hex_dlm_crc);

    addr_len = g_flash_block_info[2].end_addr - g_flash_block_info[2].start_addr;
    if ((addr_len >= 4) && (addr_len < MAX_MP_FIRMWARE_SIZE)) {
        mp_crc = calc_crc32(0, addr_len -3, mp_fw);
        hex_mp_crc = ((mp_fw[addr_len - 3] << 24) + (mp_fw[addr_len - 2] << 16) + \
            (mp_fw[addr_len - 1] << 8) + mp_fw[addr_len]);
    }
    else {
        check_fail = true;
        TPD_INFO("get mp addr len error addr_len = 0x%X\n", addr_len);
    }
    TPD_DEBUG("mp block, driver crc = 0x%x, hex mp crc = 0x%x\n", mp_crc, hex_mp_crc);

    addr_len = g_flash_block_info[3].end_addr - g_flash_block_info[3].start_addr;
    if ((addr_len >= 4) && (addr_len < MAX_GESTURE_FIRMWARE_SIZE)) {
        gesture_crc = calc_crc32(0, addr_len -3, gesture_fw);
        hex_gesture_crc = ((gesture_fw[addr_len - 3] << 24) + (gesture_fw[addr_len - 2] << 16) + \
            (gesture_fw[addr_len - 1] << 8) + gesture_fw[addr_len]);
    }
    else {
        check_fail = true;
        TPD_INFO("get gesture addr len error addr_len = 0x%X\n", addr_len);
    }
    TPD_DEBUG("gesture block, driver crc = 0x%x, hex gesture crc = 0x%x\n", gesture_crc, hex_gesture_crc);

    if ((check_fail) || (ap_crc != hex_ap_crc) || (mp_crc != hex_mp_crc)\
        || (dlm_crc != hex_dlm_crc) || (gesture_crc != hex_gesture_crc)) {
        TPD_INFO("crc erro use header file data check_fail = %d\n", check_fail);
        TPD_INFO("ap block, driver crc = 0x%X, hex ap crc = 0x%X\n", ap_crc, hex_ap_crc);
        TPD_INFO("dlm block, driver crc = 0x%X, hex dlm crc = 0x%X\n", dlm_crc, hex_dlm_crc);
        TPD_INFO("mp block, driver crc = 0x%x, hex mp crc = 0x%x\n", mp_crc, hex_mp_crc);
        TPD_INFO("gesture block, driver crc = 0x%x, hex gesture crc = 0x%x\n", gesture_crc, hex_gesture_crc);
        return -1;
    }
    else {
        TPD_DEBUG("check file crc ok\n");
        return 0;
    }
}
static int read_download(uint32_t start, uint32_t size, uint8_t *r_buf, uint32_t r_len)
{
    int res = 0;
    int addr = 0;
    int i = 0;
    uint32_t end = start + size;
    uint8_t *buf = NULL;

    buf = (uint8_t*)kmalloc(sizeof(uint8_t) * r_len + 4, GFP_KERNEL);
    if (ERR_ALLOC_MEM(buf)) {
        TPD_INFO("malloc read_ap_buf error\n");
        return -1;
    }
    memset(buf, 0xFF, (int)sizeof(uint8_t) * r_len + 4);
    for (addr = start, i = 0; addr < end; i += r_len, addr += r_len) {
        if ((addr + r_len) > end) {
            r_len = end % r_len;
        }
        buf[0] = 0x25;
        buf[3] = (char)((addr & 0x00FF0000) >> 16);
        buf[2] = (char)((addr & 0x0000FF00) >> 8);
        buf[1] = (char)((addr & 0x000000FF));
        if (core_write(core_config->slave_i2c_addr, buf, 4)) {
            TPD_INFO("Failed to write data via SPI\n");
            res = -EIO;
            goto error;
        }
        res = core_read(core_config->slave_i2c_addr, buf, r_len);
        if (res < 0) {
            TPD_INFO("Failed to read data via SPI\n");
            res = -EIO;
            goto error;
        }
        memcpy(r_buf + i, buf, r_len);
    }
error:
    ipio_kfree((void **)&buf);
    return res;
}

static int write_download(uint32_t start, uint32_t size, uint8_t *w_buf, uint32_t w_len)
{
    int res = 0;
    int addr = 0;
    int i = 0;
    int update_status = 0;
    int end = 0;
    int j = 0;
    uint8_t *buf = NULL;
    end = start + size;

    buf = (uint8_t*)kmalloc(sizeof(uint8_t) * w_len + 4, GFP_KERNEL);
    if (ERR_ALLOC_MEM(buf)) {
        TPD_INFO("malloc read_ap_buf error\n");
        return -1;
    }
    memset(buf, 0xFF, (int)sizeof(uint8_t) * w_len + 4);
    for (addr = start, i = 0; addr < end; addr += w_len, i += w_len) {
        if ((addr + w_len) > end) {
            w_len = end % w_len;
        }
        buf[0] = 0x25;
        buf[3] = (char)((addr & 0x00FF0000) >> 16);
        buf[2] = (char)((addr & 0x0000FF00) >> 8);
        buf[1] = (char)((addr & 0x000000FF));
        for (j = 0; j < w_len; j++)
            buf[4 + j] = w_buf[i + j];

        if (core_write(core_config->slave_i2c_addr, buf, w_len + 4)) {
            TPD_INFO("Failed to write data via SPI, address = 0x%X, start_addr = 0x%X, end_addr = 0x%X\n",
                (int)addr, 0, end);
            res = -EIO;
            goto write_error;
        }

        update_status = (i * 101) / end;
        //printk("%cupgrade firmware(mp code), %02d%c", 0x0D, update_status, '%');
    }
write_error:
    ipio_kfree((void **)&buf);
    return res;
}

static int host_download_dma_check(int block)
{
    int count = 50;
    uint8_t ap_block = 0;
    uint8_t dlm_block = 1;
    uint32_t start_addr = 0;
    uint32_t block_size = 0;
    uint32_t busy = 0;
    uint32_t reg_data = 0;

    if (block == ap_block) {
        start_addr = 0;
        block_size = MAX_AP_FIRMWARE_SIZE - 0x4;
    } else if (block == dlm_block) {
        start_addr = DLM_START_ADDRESS;
        block_size = MAX_DLM_FIRMWARE_SIZE;
    }
    /* dma_ch1_start_clear */
    core_config_ice_mode_write(0x072103, 0x2, 1);
    /* dma1 src1 adress */
    core_config_ice_mode_write(0x072104, start_addr, 4);
    /* dma1 src1 format */
    core_config_ice_mode_write(0x072108, 0x80000001, 4);
    /* dma1 dest address */
    core_config_ice_mode_write(0x072114, 0x00030000, 4);
    /* dma1 dest format */
    core_config_ice_mode_write(0x072118, 0x80000000, 4);
    /* Block size*/
    core_config_ice_mode_write(0x07211C, block_size, 4);
    /* crc off */
    core_config_ice_mode_write(0x041014, 0x00000000, 4);
    /* dma crc */
    core_config_ice_mode_write(0x041048, 0x00000001, 4);
    /* crc on */
    core_config_ice_mode_write(0x041014, 0x00010000, 4);
    /* Dma1 stop */
    core_config_ice_mode_write(0x072100, 0x00000000, 4);
    /* clr int */
    core_config_ice_mode_write(0x048006, 0x1, 1);
    /* Dma1 start */
    core_config_ice_mode_write(0x072100, 0x01000000, 4);

    /* Polling BIT0 */
    while (count > 0) {
        mdelay(1);
        busy = core_config_read_write_onebyte(0x048006);
        reg_data = core_config_ice_mode_read(0x072100);
        TPD_DEBUG("0x072100 reg_data = 0x%X busy = 0x%X\n", reg_data, busy);
        if ((busy & 0x01) == 1)
            break;

        count--;
    }

    if (count <= 0) {
        TPD_INFO("BIT0 is busy\n");
        reg_data = core_config_ice_mode_read(0x072100);
        TPD_INFO("0x072100 reg_data = 0x%X\n", reg_data);
        //return -1;
    }

    return core_config_ice_mode_read(0x04101C);
}

int host_download(void *chip_data, bool mode)
{
    int res = 0;
    int ap_crc = 0;
    int dlm_crc = 0;

    int ap_dma = 0;
    int dlm_dma = 0;
    int method = 0;
    uint8_t *buf = NULL;
    uint8_t *read_ap_buf = NULL;
    uint8_t *read_dlm_buf = NULL;
    uint8_t *read_mp_buf = NULL;
    uint8_t *read_gesture_buf = NULL;
    uint8_t *gesture_ap_buf = NULL;
    uint32_t reg_data = 0;
    int retry = 100;
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;

    if(!chip_info) {
        TPD_INFO("%s:chip_data is null\n", __func__);
        return -EINVAL;
    }

    read_ap_buf = (uint8_t*)vmalloc(MAX_AP_FIRMWARE_SIZE);
    if (ERR_ALLOC_MEM(read_ap_buf)) {
        TPD_INFO("malloc read_ap_buf error\n");
        goto out;
    }
    memset(read_ap_buf, 0xFF, MAX_AP_FIRMWARE_SIZE);
    //create ap buf
    read_dlm_buf = (uint8_t*)vmalloc(MAX_DLM_FIRMWARE_SIZE);
    if (ERR_ALLOC_MEM(read_dlm_buf)) {
        TPD_INFO("malloc read_dlm_buf error\n");
        goto out;
    }
    memset(read_dlm_buf, 0xFF, MAX_DLM_FIRMWARE_SIZE);
    //create mp buf
    read_mp_buf = (uint8_t*)vmalloc(MAX_MP_FIRMWARE_SIZE);
    if (ERR_ALLOC_MEM(read_mp_buf)) {
        TPD_INFO("malloc read_mp_buf error\n");
        goto out;
    }
    memset(read_mp_buf, 0xFF, MAX_MP_FIRMWARE_SIZE);
    //create buf
    buf = (uint8_t*)vmalloc(sizeof(uint8_t)*0x10000+4);
    if (ERR_ALLOC_MEM(buf)) {
        TPD_INFO("malloc buf error\n");
        goto out;
    }
    memset(buf, 0xFF, (int)sizeof(uint8_t) * 0x10000+4);
    //create gesture buf
    read_gesture_buf = (uint8_t*)vmalloc(core_gesture->ap_length);
    if (ERR_ALLOC_MEM(read_gesture_buf)) {
        TPD_INFO("malloc read_gesture_buf error\n");
        goto out;
    }
    gesture_ap_buf = (uint8_t*)vmalloc(core_gesture->ap_length);
    if (ERR_ALLOC_MEM(gesture_ap_buf)) {
        TPD_INFO("malloc gesture_ap_buf error\n");
        goto out;
    }
    res = core_config_ice_mode_enable();
    if (res < 0) {
        TPD_INFO("Failed to enter ICE mode, res = %d\n", res);
        goto out;
    }

    res = check_hex_crc();
    if (res < 0) {
        TPD_INFO("crc erro use header file data\n");
        if (core_firmware_get_h_file_data() < 0) {
            TPD_INFO("Failed to get h file data\n");
        }
        res = 0;
    }

    method = core_config_ice_mode_read(core_config->pid_addr);
    method = method & 0xff;
    TPD_DEBUG("method of calculation for crc = %x\n", method);
    core_firmware->enter_mode = -1;

    chip_info->esd_check_enabled = false;
    chip_info->irq_timer = jiffies;    //reset esd check trigger base time

    memset(gesture_ap_buf, 0xFF, core_gesture->ap_length);
    TPD_INFO("core_gesture->entry = %d\n", core_gesture->entry);
    memset(read_gesture_buf, 0xFF, core_gesture->ap_length);
    //TPD_INFO("Upgrade firmware written data into AP code directly\n");
    core_config_ice_mode_write(0x5100C, 0x81, 1);
    core_config_ice_mode_write(0x5100C, 0x98, 1);
    while(retry--) {
        reg_data = core_config_read_write_onebyte(0x51018);
        if (reg_data == 0x5A) {
            TPD_DEBUG("check wdt close ok 0x51018 read 0x%X\n", reg_data);
            break;
        }
        mdelay(10);
    }
    if (retry <= 0) {
        TPD_INFO("check wdt close error 0x51018 read 0x%X\n", reg_data);
    }
    core_config_ice_mode_write(0x5100C, 0x00, 1);

    if(core_fr->actual_fw_mode == P5_0_FIRMWARE_TEST_MODE)
    {
        core_firmware->enter_mode = P5_0_FIRMWARE_TEST_MODE;
        /* write hex to the addr of MP code */
        TPD_DEBUG("Writing data into MP code ...\n");
        if(write_download(0, MAX_MP_FIRMWARE_SIZE, mp_fw, SPI_UPGRADE_LEN) < 0)
        {
            TPD_INFO("SPI Write MP code data error\n");
        }
        if(read_download(0, MAX_MP_FIRMWARE_SIZE, read_mp_buf, SPI_UPGRADE_LEN))
        {
            TPD_INFO("SPI Read MP code data error\n");
        }
        if(memcmp(mp_fw, read_mp_buf, MAX_MP_FIRMWARE_SIZE) == 0)
        {
            TPD_INFO("Check MP Mode upgrade: PASS\n");
        }
        else
        {
            TPD_INFO("Check MP Mode upgrade: FAIL\n");
            res = UPDATE_FAIL;
            goto out;
        }
    }
    else if(core_gesture->entry)
    {
        //int i;
        if(mode)
        {
            core_firmware->enter_mode = P5_0_FIRMWARE_GESTURE_MODE;
            /* write hex to the addr of Gesture code */
            //TPD_INFO("Writing data into Gesture code ...\n");
            if(write_download(core_gesture->ap_start_addr, core_gesture->length, gesture_fw, core_gesture->length) < 0)
            {
                TPD_INFO("SPI Write Gesture code data error\n");
            }
            if(read_download(core_gesture->ap_start_addr, core_gesture->length, read_gesture_buf, core_gesture->length))
            {
                TPD_INFO("SPI Read Gesture code data error\n");
            }
            if(memcmp(gesture_fw, read_gesture_buf, core_gesture->length) == 0)
            {
                TPD_INFO("Check Gesture Mode upgrade: PASS\n");
            }
            else
            {
                TPD_INFO("Check Gesture Mode upgrade: FAIL\n");
                res = UPDATE_FAIL;
                goto out;
            }
        }
        else{
            core_firmware->enter_mode = P5_0_FIRMWARE_DEMO_MODE;
            /* write hex to the addr of AP code */
            memcpy(gesture_ap_buf, ap_fw + core_gesture->ap_start_addr, core_gesture->ap_length);
            //TPD_INFO("Writing data into AP code ...\n");
            if(write_download(core_gesture->ap_start_addr, core_gesture->ap_length, gesture_ap_buf, core_gesture->ap_length) < 0)
            {
                TPD_INFO("SPI Write AP code data error\n");
            }
            if(read_download(core_gesture->ap_start_addr, core_gesture->ap_length, read_ap_buf, core_gesture->ap_length))
            {
                TPD_INFO("SPI Read AP code data error\n");
            }
            if(memcmp(gesture_ap_buf, read_ap_buf, core_gesture->ap_length) == 0)
            {
                TPD_INFO("Check AP Mode upgrade: PASS\n");
            }
            else
            {
                TPD_INFO("Check AP Mode upgrade: FAIL\n");
                res = UPDATE_FAIL;
                goto out;
            }
        }
    }
    else
    {
        core_firmware->enter_mode = P5_0_FIRMWARE_DEMO_MODE;
        /* write hex to the addr of AP code */
        //TPD_INFO("Writing data into AP code ...\n");
        if(write_download(0, MAX_AP_FIRMWARE_SIZE, ap_fw, SPI_UPGRADE_LEN) < 0)
        {
            TPD_INFO("SPI Write AP code data error\n");
        }
        /* write hex to the addr of DLM code */
        //TPD_INFO("Writing data into DLM code ...\n");
        if(write_download(DLM_START_ADDRESS, MAX_DLM_FIRMWARE_SIZE, dlm_fw, SPI_UPGRADE_LEN) < 0)
        {
            TPD_INFO("SPI Write DLM code data error\n");
        }
        /* Check AP/DLM mode Buffer data */
        if (method >= CORE_TYPE_E) {
            ap_crc = calc_crc32(0, MAX_AP_FIRMWARE_SIZE - 4, ap_fw);
            ap_dma = host_download_dma_check(0);

            dlm_crc = calc_crc32(0, MAX_DLM_FIRMWARE_SIZE, dlm_fw);
            dlm_dma = host_download_dma_check(1);

            TPD_INFO("AP CRC %s (%x) : (%x)\n",
                (ap_crc != ap_dma ? "Invalid !" : "Correct !"), ap_crc, ap_dma);

            TPD_INFO("DLM CRC %s (%x) : (%x)\n",
                (dlm_crc != dlm_dma ? "Invalid !" : "Correct !"), dlm_crc, dlm_dma);

            if (CHECK_EQUAL(ap_crc, ap_dma) == UPDATE_FAIL ||
                    CHECK_EQUAL(dlm_crc, dlm_dma) == UPDATE_FAIL ) {
                TPD_INFO("Check AP/DLM Mode upgrade: FAIL read data check\n");
                res = UPDATE_FAIL;
                read_download(0, MAX_AP_FIRMWARE_SIZE, read_ap_buf, SPI_UPGRADE_LEN);
                read_download(DLM_START_ADDRESS, MAX_DLM_FIRMWARE_SIZE, read_dlm_buf, SPI_UPGRADE_LEN);

                if (memcmp(ap_fw, read_ap_buf, MAX_AP_FIRMWARE_SIZE) != 0 ||
                        memcmp(dlm_fw, read_dlm_buf, MAX_DLM_FIRMWARE_SIZE) != 0) {
                    TPD_INFO("Check AP/DLM Mode upgrade: FAIL\n");
                    res = UPDATE_FAIL;
                    goto out;
                } else {
                    TPD_INFO("Check AP/DLM Mode upgrade: SUCCESS\n");
                    res = 0;
                }
                //goto out;
            }
        } else {
            read_download(0, MAX_AP_FIRMWARE_SIZE, read_ap_buf, SPI_UPGRADE_LEN);
            read_download(DLM_START_ADDRESS, MAX_DLM_FIRMWARE_SIZE, read_dlm_buf, SPI_UPGRADE_LEN);

            if (memcmp(ap_fw, read_ap_buf, MAX_AP_FIRMWARE_SIZE) != 0 ||
                    memcmp(dlm_fw, read_dlm_buf, MAX_DLM_FIRMWARE_SIZE) != 0) {
                TPD_INFO("Check AP/DLM Mode upgrade: FAIL\n");
                res = UPDATE_FAIL;
                goto out;
            } else {
                TPD_INFO("Check AP/DLM Mode upgrade: SUCCESS\n");
            }
        }
        if (1 == core_firmware->esd_fail_enter_gesture) {
            TPD_INFO("set 0x25FF8 = 0xF38A94EF for gesture\n");
            core_config_ice_mode_write(0x25FF8, 0xF38A94EF, 4);
        }
    }

    core_config_ice_mode_write(0x5100C, 0x01, 1);
    while(retry--) {
        reg_data = core_config_read_write_onebyte(0x51018);
        if (reg_data == 0xA5) {
            TPD_DEBUG("check wdt open ok 0x51018 read 0x%X\n", reg_data);
            break;
        }
        mdelay(10);
    }
    if (retry <= 0) {
        TPD_INFO("check wdt open error 0x51018 read 0x%X retry set\n", reg_data);
        core_config_ice_mode_write(0x5100C, 0x01, 1);
    }
    if(core_gesture->entry != true)
    {
        /* ice mode code reset */
        TPD_DEBUG("Doing code reset ...\n");
        core_config_ice_mode_write(0x40040, 0xAE, 1);
    }
    core_config_ice_mode_disable();
    if(core_fr->actual_fw_mode == P5_0_FIRMWARE_TEST_MODE)
        mdelay(200);
    else
        mdelay(40);
out:
    chip_info->esd_check_enabled = true;

    ipio_vfree((void **)&buf);
    ipio_vfree((void **)&read_ap_buf);
    ipio_vfree((void **)&read_dlm_buf);
    ipio_vfree((void **)&read_mp_buf);
    ipio_vfree((void **)&read_gesture_buf);
    ipio_vfree((void **)&gesture_ap_buf);
    if (res == UPDATE_FAIL) {
        core_config_ice_mode_disable();
    }
    return res;
}
EXPORT_SYMBOL(host_download);

int core_load_gesture_code(void)
{
    int res = 0;
    int i = 0;
    uint8_t temp[64] = {0};

    core_gesture->entry = true;
    core_firmware->core_version = (ap_fw[0xFFF4] << 24) + (ap_fw[0xFFF5] << 16) + (ap_fw[0xFFF6] << 8) + ap_fw[0xFFF7];
    TPD_INFO("core_firmware->core_version = 0x%X\n", core_firmware->core_version);
    if (core_firmware->core_version >= 0x01000600) {
        core_gesture->area_section = (ap_fw[0xFFC4 + 3] << 24) + (ap_fw[0xFFC4 + 2] << 16) + (ap_fw[0xFFC4 + 1] << 8) + ap_fw[0xFFC4];
        core_gesture->ap_start_addr = (ap_fw[0xFFC4 + 7] << 24) + (ap_fw[0xFFC4 + 6] << 16) + (ap_fw[0xFFC4 + 5] << 8) + ap_fw[0xFFC4 + 4];
        core_gesture->start_addr = (ap_fw[0xFFC4 + 15] << 24) + (ap_fw[0xFFC4 + 14] << 16) + (ap_fw[0xFFC4 + 13] << 8) + ap_fw[0xFFC4 + 12];
    }
    else {
        core_gesture->area_section = (ap_fw[0xFFCF] << 24) + (ap_fw[0xFFCE] << 16) + (ap_fw[0xFFCD] << 8) + ap_fw[0xFFCC];
        core_gesture->ap_start_addr = (ap_fw[0xFFD3] << 24) + (ap_fw[0xFFD2] << 16) + (ap_fw[0xFFD1] << 8) + ap_fw[0xFFD0];
        core_gesture->start_addr = (ap_fw[0xFFDB] << 24) + (ap_fw[0xFFDA] << 16) + (ap_fw[0xFFD9] << 8) + ap_fw[0xFFD8];
    }
    core_gesture->length = MAX_GESTURE_FIRMWARE_SIZE;
    core_gesture->ap_length = MAX_GESTURE_FIRMWARE_SIZE;
    core_fr->isEnableFR = false;
    ilitek_platform_disable_irq();
    TPD_DEBUG("gesture_start_addr = 0x%x, core_gesture->length = 0x%x\n", core_gesture->start_addr, core_gesture->length);
    TPD_DEBUG("area = %d, ap_start_addr = 0x%x, core_gesture->ap_length = 0x%x\n", core_gesture->area_section, core_gesture->ap_start_addr, core_gesture->ap_length);
    //write load gesture flag
    temp[0] = 0x01;
    temp[1] = 0x0A;
    temp[2] = 0x03;
    if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
        TPD_INFO("write command error\n");
    }
    //enter gesture cmd lpwg start
    temp[0] = 0x01;
    temp[1] = 0x0A;
    temp[2] = core_gesture->mode + 1;
    if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
        TPD_INFO("write command error\n");
    }
    for(i = 0; i < 5; i++)
    {
        temp[0] = 0xF6;
        temp[1] = 0x0A;
        TPD_DEBUG("write prepare gesture command 0xF6 0x0A \n");
        if ((core_write(core_config->slave_i2c_addr, temp, 2)) < 0) {
            TPD_INFO("write prepare gesture command error\n");
        }
        mdelay(i*50);
        temp[0] = 0x01;
        temp[1] = 0x0A;
        temp[2] = 0x05;
        if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
            TPD_INFO("write command error\n");
        }
        if ((core_read(core_config->slave_i2c_addr, temp, 2)) < 0) {
            TPD_INFO("Read command error\n");
        }
        if(temp[0] == 0x91)
        {
            TPD_DEBUG("check fw ready\n");
            break;
        }
    }
    if(temp[0] != 0x91)
            TPD_INFO("FW is busy, error\n");

    //load gesture code
    if (core_config_ice_mode_enable() < 0) {
        TPD_INFO("Failed to enter ICE mode\n");
        res = -1;
        goto out;
    }
    host_download(ipd, true);

    temp[0] = 0x01;
    temp[1] = 0x0A;
    temp[2] = 0x06;
    if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
        TPD_INFO("write command error\n");
    }
out:
    core_fr->isEnableFR = true;
    ilitek_platform_enable_irq();
    return res;
}
EXPORT_SYMBOL(core_load_gesture_code);

#if 0
int core_load_ap_code(void)
{
    int res = 0;
    int i = 0;
    uint8_t temp[64] = {0};
    uint32_t gesture_end_addr = 0;
    uint32_t gesture_start_addr = 0;
    uint32_t ap_start_addr = 0;
    uint32_t ap_end_addr = 0;
    uint32_t area = 0;
    core_gesture->entry = true;
    core_fr->isEnableFR = false;

    ilitek_platform_disable_irq();
    //Write Load AP Flag
    temp[0] = 0x01;
    temp[1] = 0x01;
    temp[2] = 0x00;
    if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
        TPD_INFO("write command error\n");
    }
    if ((core_read(core_config->slave_i2c_addr, temp, 20)) < 0) {
        TPD_INFO("Read command error\n");
    }
    area = (temp[0] << 24) + (temp[1] << 16) + (temp[2] << 8) + temp[3];
    ap_start_addr = (temp[4] << 24) + (temp[5] << 16) + (temp[6] << 8) + temp[7];
    ap_end_addr = (temp[8] << 24) + (temp[9] << 16) + (temp[10] << 8) + temp[11];
    gesture_start_addr = (temp[12] << 24) + (temp[13] << 16) + (temp[14] << 8) + temp[15];
    gesture_end_addr = (temp[16] << 24) + (temp[17] << 16) + (temp[18] << 8) + temp[19];
    TPD_INFO("gesture_start_addr = 0x%x, gesture_end_addr = 0x%x\n", gesture_start_addr, gesture_end_addr);
    TPD_INFO("area = %d, ap_start_addr = 0x%x, ap_end_addr = 0x%x\n", area, ap_start_addr, ap_end_addr);
    //Leave Gesture Cmd LPWG Stop
    temp[0] = 0x01;
    temp[1] = 0x0A;
    temp[2] = 0x00;
    if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
        TPD_INFO("write command error\n");
    }
    for(i = 0; i < 20; i++)
    {
        mdelay(i*100+100);
        temp[0] = 0x01;
        temp[1] = 0x0A;
        temp[2] = 0x05;
        if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
            TPD_INFO("write command error\n");
        }
        if ((core_read(core_config->slave_i2c_addr, temp, 1)) < 0) {
            TPD_INFO("Read command error\n");
        }
        if(temp[0] == 0x91)
        {
            TPD_INFO("check fw ready\n");
            break;
        }
    }
    if(i == 3 && temp[0] != 0x01)
            TPD_INFO("FW is busy, error\n");

    //load AP code
    if (core_config_ice_mode_enable() < 0) {
        TPD_INFO("Failed to enter ICE mode\n");
        res = -1;
        goto out;
    }
    res = host_download(ipd, false);

    temp[0] = 0x01;
    temp[1] = 0x0A;
    temp[2] = 0x06;
    if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
        TPD_INFO("write command error\n");
    }
out:
    core_fr->isEnableFR = true;
    ilitek_platform_enable_irq();
    core_gesture->entry = false;
    return res;
}
EXPORT_SYMBOL(core_load_ap_code);
#endif
#endif

#ifdef HOST_DOWNLOAD
int core_firmware_get_project_h_file_data(void)
{
    static int do_once = 0;
    int ret = 0;

    if (do_once == 0) {
        do_once = 1;
        if(ipd->p_firmware_headfile->firmware_data) {
            if (MAX_CTPM_FW_LEN >= ipd->p_firmware_headfile->firmware_size) {
                memcpy(CTPM_FW, ipd->p_firmware_headfile->firmware_data, ipd->p_firmware_headfile->firmware_size);
                ret = 0;
            } else {
                ipio_info("p_firmware_headfile->firmware_data is more than %d,exit firmware update!\n", MAX_CTPM_FW_LEN);
                ret = -1;
            }
        } else {
            TPD_INFO("p_firmware_headfile->firmware_data is NULL! exit firmware update!\n");
            ret = -1;
        }
    }
    return ret;
}

int core_firmware_get_h_file_data(void)
{
    int res = 0;
    int i = 0;
    int j = 0;
    int block = 0;
    int blen = 0;
    int bindex = 0;

    flash_fw = (uint8_t*)vmalloc(MAX_FLASH_FW_LEN);//256K
    if (ERR_ALLOC_MEM(flash_fw)) {
        TPD_INFO("Failed to allocate flash_fw memory, %ld\n", PTR_ERR(flash_fw));
        res = -ENOMEM;
        goto out;
    }

    memset(flash_fw, 0xff, (int)sizeof(uint8_t) * MAX_FLASH_FW_LEN);
    res = core_firmware_get_project_h_file_data();
    if (res < 0){
        goto get_h_file_fail;
    }
    /* Fill data into buffer */
    for (i = 0; i < ARRAY_SIZE(CTPM_FW) - 64; i++) {
        flash_fw[i] = CTPM_FW[i + 64];
    }
    memcpy(ap_fw, flash_fw, MAX_AP_FIRMWARE_SIZE);
    memcpy(dlm_fw, flash_fw + DLM_HEX_ADDRESS, MAX_DLM_FIRMWARE_SIZE);

    core_firmware->core_version = (ap_fw[0xFFF4] << 24) + (ap_fw[0xFFF5] << 16) + (ap_fw[0xFFF6] << 8) + ap_fw[0xFFF7];
    TPD_INFO("core_firmware->core_version = 0x%X\n", core_firmware->core_version);
    if (core_firmware->core_version >= 0x01000600) {
        core_gesture->area_section = (ap_fw[0xFFC4 + 3] << 24) + (ap_fw[0xFFC4 + 2] << 16) + (ap_fw[0xFFC4 + 1] << 8) + ap_fw[0xFFC4];
        core_gesture->ap_start_addr = (ap_fw[0xFFC4 + 7] << 24) + (ap_fw[0xFFC4 + 6] << 16) + (ap_fw[0xFFC4 + 5] << 8) + ap_fw[0xFFC4 + 4];
        core_gesture->start_addr = (ap_fw[0xFFC4 + 15] << 24) + (ap_fw[0xFFC4 + 14] << 16) + (ap_fw[0xFFC4 + 13] << 8) + ap_fw[0xFFC4 + 12];
    }
    else {
        core_gesture->area_section = (ap_fw[0xFFCF] << 24) + (ap_fw[0xFFCE] << 16) + (ap_fw[0xFFCD] << 8) + ap_fw[0xFFCC];
        core_gesture->ap_start_addr = (ap_fw[0xFFD3] << 24) + (ap_fw[0xFFD2] << 16) + (ap_fw[0xFFD1] << 8) + ap_fw[0xFFD0];
        core_gesture->start_addr = (ap_fw[0xFFDB] << 24) + (ap_fw[0xFFDA] << 16) + (ap_fw[0xFFD9] << 8) + ap_fw[0xFFD8];
    }
    core_gesture->length = MAX_GESTURE_FIRMWARE_SIZE;
    core_gesture->ap_length = MAX_GESTURE_FIRMWARE_SIZE;

    TPD_INFO("gesture_start_addr = 0x%x, length = 0x%x\n", core_gesture->start_addr, core_gesture->length);
    TPD_INFO("area = %d, ap_start_addr = 0x%x, ap_length = 0x%x\n", core_gesture->area_section, core_gesture->ap_start_addr, core_gesture->ap_length);
    TPD_INFO("MP_HEX_ADDRESS + MAX_MP_FIRMWARE_SIZE = 0x%X\n", MP_HEX_ADDRESS + MAX_MP_FIRMWARE_SIZE);
    memcpy(mp_fw, flash_fw + MP_HEX_ADDRESS, MAX_MP_FIRMWARE_SIZE);
    TPD_INFO("core_gesture->start_addr + core_gesture->length = 0x%X\n", core_gesture->start_addr + MAX_MP_FIRMWARE_SIZE);
    memcpy(gesture_fw, flash_fw + core_gesture->start_addr, core_gesture->length);

    /* Extract block info */
    block = CTPM_FW[33];
    for (i = 0; i < 4; i++) {
        g_flash_block_info[i].start_addr = 0;
        g_flash_block_info[i].end_addr = 0;
    }
    if (block > 0) {
        core_firmware->hasBlockInfo = true;

        /* Initialize block's index and length */
        blen = 6;
        bindex = 34;

        for (i = 0; i < block; i++) {
            for (j = 0; j < blen; j++) {
                if (j < 3)
                    g_flash_block_info[i].start_addr =
                        (g_flash_block_info[i].start_addr << 8) | CTPM_FW[bindex + j];
                else
                    g_flash_block_info[i].end_addr =
                        (g_flash_block_info[i].end_addr << 8) | CTPM_FW[bindex + j];
            }

            bindex += blen;
        }
    }
    check_hex_crc();

get_h_file_fail:
    if (!ERR_ALLOC_MEM(flash_fw)) {
        vfree(flash_fw);
        flash_fw = NULL;
    }
out:

    return res;
}
int core_firmware_boot_host_download(void *chip_data)
{
    int res = 0;
    int i = 0;

    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;

    if(!chip_info) {
        TPD_INFO("%s:chip_data is null\n", __func__);
        return -EINVAL;
    }

    TPD_INFO("MAX_AP_FIRMWARE_SIZE + MAX_DLM_FIRMWARE_SIZE + MAX_MP_FIRMWARE_SIZE = 0x%X\n", MAX_AP_FIRMWARE_SIZE + MAX_DLM_FIRMWARE_SIZE + MAX_MP_FIRMWARE_SIZE);
    //flash_fw = kcalloc(MAX_AP_FIRMWARE_SIZE + MAX_DLM_FIRMWARE_SIZE + MAX_MP_FIRMWARE_SIZE, sizeof(uint8_t), GFP_KERNEL);
    flash_fw = (uint8_t*)vmalloc(MAX_FLASH_FW_LEN);
    if (ERR_ALLOC_MEM(flash_fw)) {
        TPD_INFO("Failed to allocate flash_fw memory, %ld\n", PTR_ERR(flash_fw));
        res = -ENOMEM;
        goto out;
    }

    memset(flash_fw, 0xff, (int)sizeof(uint8_t) * MAX_FLASH_FW_LEN);

    TPD_INFO("BOOT: Starting to upgrade firmware ...\n");

    core_firmware->isUpgrading = true;
    core_firmware->update_status = 0;

    res = core_firmware_get_project_h_file_data();
    if(res < 0) {
        goto out;
    }
    /* Fill data into buffer */
    for (i = 0; i < ARRAY_SIZE(CTPM_FW) - 64; i++) {
        flash_fw[i] = CTPM_FW[i + 64];
    }
    memcpy(ap_fw, flash_fw, MAX_AP_FIRMWARE_SIZE);
    memcpy(dlm_fw, flash_fw + DLM_HEX_ADDRESS, MAX_DLM_FIRMWARE_SIZE);

    core_firmware->core_version = (ap_fw[0xFFF4] << 24) + (ap_fw[0xFFF5] << 16) + (ap_fw[0xFFF6] << 8) + ap_fw[0xFFF7];
    TPD_INFO("core_firmware->core_version = 0x%X\n", core_firmware->core_version);
    if (core_firmware->core_version >= 0x01000600) {
        core_gesture->area_section = (ap_fw[0xFFC4 + 3] << 24) + (ap_fw[0xFFC4 + 2] << 16) + (ap_fw[0xFFC4 + 1] << 8) + ap_fw[0xFFC4];
        core_gesture->ap_start_addr = (ap_fw[0xFFC4 + 7] << 24) + (ap_fw[0xFFC4 + 6] << 16) + (ap_fw[0xFFC4 + 5] << 8) + ap_fw[0xFFC4 + 4];
        core_gesture->start_addr = (ap_fw[0xFFC4 + 15] << 24) + (ap_fw[0xFFC4 + 14] << 16) + (ap_fw[0xFFC4 + 13] << 8) + ap_fw[0xFFC4 + 12];
    }
    else {
        core_gesture->area_section = (ap_fw[0xFFCF] << 24) + (ap_fw[0xFFCE] << 16) + (ap_fw[0xFFCD] << 8) + ap_fw[0xFFCC];
        core_gesture->ap_start_addr = (ap_fw[0xFFD3] << 24) + (ap_fw[0xFFD2] << 16) + (ap_fw[0xFFD1] << 8) + ap_fw[0xFFD0];
        core_gesture->start_addr = (ap_fw[0xFFDB] << 24) + (ap_fw[0xFFDA] << 16) + (ap_fw[0xFFD9] << 8) + ap_fw[0xFFD8];
    }

    core_gesture->ap_length = MAX_GESTURE_FIRMWARE_SIZE;
    core_gesture->length = MAX_GESTURE_FIRMWARE_SIZE;
    TPD_INFO("gesture_start_addr = 0x%x, length = 0x%x\n", core_gesture->start_addr, core_gesture->length);
    TPD_INFO("area = %d, ap_start_addr = 0x%x, ap_length = 0x%x\n", core_gesture->area_section, core_gesture->ap_start_addr, core_gesture->ap_length);
    TPD_INFO("MP_HEX_ADDRESS + MAX_MP_FIRMWARE_SIZE = 0x%X\n", MP_HEX_ADDRESS + MAX_MP_FIRMWARE_SIZE);
    memcpy(mp_fw, flash_fw + MP_HEX_ADDRESS, MAX_MP_FIRMWARE_SIZE);
    TPD_INFO("core_gesture->start_addr + core_gesture->length = 0x%X\n", core_gesture->start_addr + MAX_MP_FIRMWARE_SIZE);
    memcpy(gesture_fw, flash_fw + core_gesture->start_addr, core_gesture->length);
    ilitek_platform_disable_irq();
    if (chip_info->hw_res->reset_gpio) {
        TPD_INFO("HW Reset: HIGH\n");
        gpio_direction_output(chip_info->hw_res->reset_gpio, 1);
        mdelay(chip_info->delay_time_high);
        TPD_INFO("HW Reset: LOW\n");
        gpio_set_value(chip_info->hw_res->reset_gpio, 0);
        mdelay(chip_info->delay_time_low);
        TPD_INFO("HW Reset: HIGH\n");
        gpio_set_value(chip_info->hw_res->reset_gpio, 1);
        mdelay(chip_info->edge_delay);
    }
    else {
        TPD_INFO("reset gpio is Invalid\n");
    }
    res = core_firmware->upgrade_func(chip_info, true);
    if (res < 0) {
        core_firmware->update_status = res;
        TPD_INFO("Failed to upgrade firmware, res = %d\n", res);
        goto out;
    }
    mdelay(20);
    TPD_INFO("mdelay 20 ms test for ftm\n");

    core_firmware->update_status = 100;
    TPD_INFO("Update firmware information...\n");
    core_config_get_chip_id();
    core_config_get_fw_ver(chip_info);
    core_config_get_protocol_ver();
    core_config_get_core_ver();
    core_config_get_tp_info();
    if (core_config->tp_info->nKeyCount > 0) {
        core_config_get_key_info();
    }
    if (chip_info->edge_limit_status) {
        core_config_edge_limit_ctrl(chip_info, true);
    }
    if (chip_info->plug_status) {
        core_config_plug_ctrl(false);
    }

out:
    //ipio_kfree((void **)&flash_fw);
    if (!ERR_ALLOC_MEM(flash_fw)) {
        vfree(flash_fw);
        flash_fw = NULL;
    }
    ipio_kfree((void **)&g_flash_sector);
    core_firmware->isUpgrading = false;
    ilitek_platform_enable_irq();

    return res;
}
EXPORT_SYMBOL(core_firmware_boot_host_download);
#endif

static int convert_hex_file(uint8_t *pBuf, uint32_t nSize, bool isIRAM)
{
    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t k = 0;
    uint32_t nLength = 0;
    uint32_t nAddr = 0;
    uint32_t nType = 0;
    uint32_t nStartAddr = 0x0;
    uint32_t nEndAddr = 0x0;
    uint32_t nChecksum = 0x0;
    uint32_t nExAddr = 0;
    int index = 0, block = 0;
    core_firmware->start_addr = 0;
    core_firmware->end_addr = 0;
    core_firmware->checksum = 0;
    core_firmware->crc32 = 0;
    core_firmware->hasBlockInfo = false;

    memset(g_flash_block_info, 0x0, sizeof(g_flash_block_info));
#ifdef HOST_DOWNLOAD
    memset(ap_fw, 0xFF, sizeof(ap_fw));
    memset(dlm_fw, 0xFF, sizeof(dlm_fw));
    memset(mp_fw, 0xFF, sizeof(mp_fw));
    memset(gesture_fw, 0xFF, sizeof(gesture_fw));
#endif
    /* Parsing HEX file */
    for (; i < nSize;) {
        int32_t nOffset;

        nLength = HexToDec(&pBuf[i + 1], 2);
        nAddr = HexToDec(&pBuf[i + 3], 4);
        nType = HexToDec(&pBuf[i + 7], 2);

        /* calculate checksum */
        for (j = 8; j < (2 + 4 + 2 + (nLength * 2)); j += 2) {
            if (nType == 0x00) {
                /* for ice mode write method */
                nChecksum = nChecksum + HexToDec(&pBuf[i + 1 + j], 2);
            }
        }

        if (nType == 0x04) {
            nExAddr = HexToDec(&pBuf[i + 9], 4);
        }

        if (nType == 0x02) {
            nExAddr = HexToDec(&pBuf[i + 9], 4);
            nExAddr = nExAddr >> 12;
        }

        if (nType == 0xAE) {
            core_firmware->hasBlockInfo = true;
            /* insert block info extracted from hex */
            if (block < 4) {
                g_flash_block_info[block].start_addr = HexToDec(&pBuf[i + 9], 6);
                g_flash_block_info[block].end_addr = HexToDec(&pBuf[i + 9 + 6], 6);
                TPD_DEBUG("Block[%d]: start_addr = %x, end = %x\n",
                    block, g_flash_block_info[block].start_addr, g_flash_block_info[block].end_addr);
            }
            block++;
        }

        nAddr = nAddr + (nExAddr << 16);
        if (pBuf[i + 1 + j + 2] == 0x0D) {
            nOffset = 2;
        } else {
            nOffset = 1;
        }

        if (nType == 0x00) {
            if (nAddr > MAX_HEX_FILE_SIZE) {
                TPD_INFO("Invalid hex format\n");
                goto out;
            }

            if (nAddr < nStartAddr) {
                nStartAddr = nAddr;
            }
            if ((nAddr + nLength) > nEndAddr) {
                nEndAddr = nAddr + nLength;
            }
            /* fill data */
            for (j = 0, k = 0; j < (nLength * 2); j += 2, k++) {
                if (isIRAM)
                {
                    #ifdef HOST_DOWNLOAD
                    if((nAddr + k) < 0x10000)
                    {
                        ap_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);
                    }
                    else if((nAddr + k) >= DLM_HEX_ADDRESS && (nAddr + k) < MP_HEX_ADDRESS )
                    {
                        if((nAddr + k) < (DLM_HEX_ADDRESS + MAX_DLM_FIRMWARE_SIZE))
                            dlm_fw[nAddr - DLM_HEX_ADDRESS + k] = HexToDec(&pBuf[i + 9 + j], 2);
                    }
                    else if((nAddr + k) >= MP_HEX_ADDRESS && ((nAddr + k) < MP_HEX_ADDRESS + MAX_MP_FIRMWARE_SIZE))
                    {
                        mp_fw[nAddr - MP_HEX_ADDRESS + k] = HexToDec(&pBuf[i + 9 + j], 2);
                    }
                    if ((nAddr + k) == (0xFFF7)) {
                        core_firmware->core_version = (ap_fw[0xFFF4] << 24) + (ap_fw[0xFFF5] << 16) + (ap_fw[0xFFF6] << 8) + ap_fw[0xFFF7];
                        TPD_INFO("core_firmware->core_version = 0x%X\n", core_firmware->core_version);
                        if (core_firmware->core_version >= 0x01000600) {
                            core_gesture->area_section = (ap_fw[0xFFC4 + 3] << 24) + (ap_fw[0xFFC4 + 2] << 16) + (ap_fw[0xFFC4 + 1] << 8) + ap_fw[0xFFC4];
                            core_gesture->ap_start_addr = (ap_fw[0xFFC4 + 7] << 24) + (ap_fw[0xFFC4 + 6] << 16) + (ap_fw[0xFFC4 + 5] << 8) + ap_fw[0xFFC4 + 4];
                            core_gesture->start_addr = (ap_fw[0xFFC4 + 15] << 24) + (ap_fw[0xFFC4 + 14] << 16) + (ap_fw[0xFFC4 + 13] << 8) + ap_fw[0xFFC4 + 12];
                        }
                        else {
                            core_gesture->area_section = (ap_fw[0xFFCF] << 24) + (ap_fw[0xFFCE] << 16) + (ap_fw[0xFFCD] << 8) + ap_fw[0xFFCC];
                            core_gesture->ap_start_addr = (ap_fw[0xFFD3] << 24) + (ap_fw[0xFFD2] << 16) + (ap_fw[0xFFD1] << 8) + ap_fw[0xFFD0];
                            core_gesture->start_addr = (ap_fw[0xFFDB] << 24) + (ap_fw[0xFFDA] << 16) + (ap_fw[0xFFD9] << 8) + ap_fw[0xFFD8];
                        }
                        core_gesture->ap_length = MAX_GESTURE_FIRMWARE_SIZE;
                        core_gesture->length = MAX_GESTURE_FIRMWARE_SIZE;
                    }
                    if((nAddr + k) >= core_gesture->start_addr && (nAddr + k) < (core_gesture->start_addr + MAX_GESTURE_FIRMWARE_SIZE))
                    {
                        gesture_fw[nAddr - core_gesture->start_addr + k] = HexToDec(&pBuf[i + 9 + j], 2);
                    }
                    #else
                    iram_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);
                    #endif
                }
                else {
                    flash_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);

                    if ((nAddr + k) != 0) {
                        index = ((nAddr + k) / flashtab->sector);
                        if (!g_flash_sector[index].data_flag) {
                            g_flash_sector[index].ss_addr = index * flashtab->sector;
                            g_flash_sector[index].se_addr =
                                (index + 1) * flashtab->sector - 1;
                            g_flash_sector[index].dlength =
                                (g_flash_sector[index].se_addr -
                                 g_flash_sector[index].ss_addr) + 1;
                            g_flash_sector[index].data_flag = true;
                        }
                    }
                }
            }
        }
        i += 1 + 2 + 4 + 2 + (nLength * 2) + 2 + nOffset;
    }
    #ifdef HOST_DOWNLOAD
        return 0;
    #endif
out:
    TPD_INFO("Failed to convert HEX data\n");
    return -1;
}

#ifdef HOST_DOWNLOAD
int core_firmware_get_hostdownload_data(void)
{
    int res = 0;

    if (!ERR_ALLOC_MEM(core_firmware->fw) && (!ERR_ALLOC_MEM(core_firmware->fw->data)) && (core_firmware->fw->size != 0) && (ipd->common_reset == 1)) {
        res = convert_hex_file((uint8_t *)core_firmware->fw->data, core_firmware->fw->size, true);
    }else {
        res = -1;
    }
    if (res < 0) {
        TPD_INFO("Failed to covert firmware data, res = %d\n", res);
    }
    return res;
}
#endif

/*
 * It would basically be called by ioctl when users want to upgrade firmware.
 */
int core_firmware_upgrade(void *chip_data, bool isIRAM)
{
    int res = 0;
    int i = 0;
    uint8_t cmd[4] = { 0 };
    struct ilitek_chip_data_9881h *chip_info = (struct ilitek_chip_data_9881h *)chip_data;

    if(!chip_info) {
        TPD_INFO("%s:chip_data is null\n", __func__);
        return -EINVAL;
    }
    if (core_firmware->isUpgrading) {
        TPD_INFO("isupgrading so return\n");
        return 0;
    }
    core_firmware->isUpgrading = true;
    core_firmware->update_status = 0;

    /* calling that function defined at init depends on chips. */
    res = core_firmware->upgrade_func(chip_info, isIRAM);
    if (res < 0) {
        TPD_INFO("Failed to upgrade firmware, res = %d\n", res);
        goto out;
    }

    //check tp set trim code status
    if (core_firmware->core_version < 0x01000600) {
        for (i = 0; i < 14; i++) {
            cmd[0] = 0x04;
            res = core_write(core_config->slave_i2c_addr, cmd, 1);
            if (res < 0) {
                TPD_INFO("Failed to write data, %d\n", res);
            }

            res = core_read(core_config->slave_i2c_addr, cmd, 3);
            TPD_INFO("read value 0x%X 0x%X 0x%X\n", cmd[0], cmd[1], cmd[2]);
            if (res < 0) {
                TPD_INFO("Failed to read tp set ddi trim code %d\n", res);
            }
            if (cmd[0] == 0x55) {
                TPD_INFO("TP set ddi trim code ok read value 0x%X i = %d\n", cmd[0], i);
                break;
            }
            else if (cmd[0] == 0x35) {
                TPD_INFO("TP set ddi trim code bypass read value 0x%X\n", cmd[0]);
                break;
            }
            mdelay(5);
        }
        if (i >= 14) {
            TPD_INFO("check TP set ddi trim code error\n");
        }
    }
    set_tp_fw_done();

    core_config_get_chip_id();
    core_config_get_fw_ver(chip_info);
    core_config_get_protocol_ver();
    core_config_get_core_ver();
    core_config_get_tp_info();
    if (core_config->tp_info->nKeyCount > 0) {
        core_config_get_key_info();
    }

    //if (chip_info->edge_limit_status) {
        core_config_edge_limit_ctrl(chip_info, chip_info->edge_limit_status);
    //}
    if (chip_info->plug_status) {
        core_config_plug_ctrl(false);
    }
    if (chip_info->lock_point_status) {
        core_config_lock_point_ctrl(false);
    }
    if (chip_info->headset_status) {
        core_config_headset_ctrl(true);
    }
out:

    //ipio_kfree((void **)&flash_fw);
    if (!ERR_ALLOC_MEM(flash_fw)) {
        vfree(flash_fw);
        flash_fw = NULL;
    }
    ipio_kfree((void **)&g_flash_sector);
    core_firmware->isUpgrading = false;
    return res;
}

int core_firmware_init(void)
{
    core_firmware = kzalloc(sizeof(*core_firmware), GFP_KERNEL);
    if (ERR_ALLOC_MEM(core_firmware)) {
        TPD_INFO("Failed to allocate core_firmware mem, %ld\n", PTR_ERR(core_firmware));
        core_firmware_remove();
        return -ENOMEM;
    }

    core_firmware->hasBlockInfo = false;
    core_firmware->isboot = false;
    core_firmware->enter_mode = -1;

    /*0x9881*/
    core_firmware->max_count = 0x1FFFF;
    core_firmware->isCRC = true;
    #ifdef HOST_DOWNLOAD
    core_firmware->upgrade_func = host_download;
    core_firmware->delay_after_upgrade = 200;
    #endif

    return 0;
}

void core_firmware_remove(void)
{
    TPD_INFO("Remove core-firmware members\n");
    ipio_kfree((void **)&core_firmware);
}
