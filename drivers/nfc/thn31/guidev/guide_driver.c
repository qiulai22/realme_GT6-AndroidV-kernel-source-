// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 TsingTeng MicroSystem Co., Ltd.
 */

#include "guide_driver.h"

/*********** PART0: Global Variables Area ***********/
struct guide_dev *guidev = NULL;
static chip_t chip_type = TMS_THN31;

/*********** PART1: Function declarations Area ***********/
//extern int xxx_probe(struct i2c_client *client, const struct i2c_device_id *id);
//extern int xxx_remove(struct i2c_client *client);
//extern int xxx_suspend(struct device *device);
//extern int xxx_resume(struct device *device);
//extern int xxx_ese_dev_init(void);
//extern void xxx_ese_dev_exit(void);

/*********** PART2: Guidev Driver Start Area ***********/
static void guidev_resource_release(struct guide_dev *gdata)
{
    gpio_free(gdata->hw_res.irq_gpio);
    gpio_free(gdata->hw_res.ven_gpio);
    if (gdata->dlpin_flag) {
        gpio_free(gdata->hw_res.download_gpio);
    }
}

static void guidev_hw_reset(void)
{
    if (!guidev->tms->set_gpio) {
        TMS_ERR("guidev->tms->set_gpio is NULL");
        return;
    }

    guidev->tms->set_gpio(guidev->hw_res.ven_gpio, OFF, WAIT_TIME_20000US, WAIT_TIME_20000US);
    guidev->tms->set_gpio(guidev->hw_res.ven_gpio, ON, WAIT_TIME_NONE, WAIT_TIME_20000US);
}

static int guidev_probe_driver(struct i2c_client *client,
                               const struct i2c_device_id *id, chip_t chip_type)
{
    int ret = SUCCESS;
    guidev_resource_release(guidev);

    switch (chip_type) {
    case TMS_THN31:
        ret = nfc_device_probe(client, id);
#if IS_ENABLED(CONFIG_TMS_ESE_DEVICE)
        ese_driver_init();
#endif
        break;

    case SAMPLE_DEV_1:
        //TMS_INFO("XXX XXX driver start probe\n");
        //ret = sample_dev_1_probe(client, id);
        //sample_dev_1_ese_driver_init();
        break;

    case SAMPLE_DEV_2:
        //TMS_INFO("XXX XXX driver start probe\n");
        //ret = sample_dev_2_probe(client, id);
        //sample_dev_2_ese_driver_init();
        break;

    default:
        TMS_ERR("Unknow chip %s[%d]\n", guidev->tms->nfc_name, chip_type);
        break;
    }

    return ret;
}

static int guidev_parse_dts(struct guide_dev *gdata)
{
    int rc;
    int ret = 0;
    struct device_node *np;

    np = gdata->dev->of_node;
    gdata->hw_res.irq_gpio = of_get_named_gpio(np, "tms,irq-gpio", 0);

    if (gpio_is_valid(gdata->hw_res.irq_gpio)) {
        rc = gpio_request(gdata->hw_res.irq_gpio, "guidev_int");

        if (rc) {
            TMS_WARN("unable to request gpio[%d] as irq\n", gdata->hw_res.irq_gpio);
        }
    } else {
        TMS_ERR("irq gpio not specified\n");
        return -EINVAL;
    }

    gdata->hw_res.ven_gpio = of_get_named_gpio_flags(np, "tms,ven-gpio", 0,
                             &gdata->hw_res.ven_flag);

    if (gpio_is_valid(gdata->hw_res.ven_gpio)) {
        rc = gpio_request(gdata->hw_res.ven_gpio, "guidev_ven");

        if (rc) {
            TMS_WARN("unable to request gpio[%d] as ven\n", gdata->hw_res.ven_gpio);
        }
    } else {
        TMS_ERR("ven gpio not specified\n");
        ret =  -EINVAL;
        goto err_ven;
    }

    gdata->hw_res.download_gpio = of_get_named_gpio_flags(np, "tms,download-gpio",
                                  0,
                                  &gdata->hw_res.download_flag);

    if (gpio_is_valid(gdata->hw_res.download_gpio)) {
        gdata->dlpin_flag = true;
        rc = gpio_request(gdata->hw_res.download_gpio, "guidev_fw_download");

        if (rc) {
            TMS_WARN("unable to request gpio[%d] as download\n",
                     gdata->hw_res.download_gpio);
        }
    } else {
        gdata->dlpin_flag = false;
        TMS_ERR("fw-download gpio not specified in dts\n");
    }

    TMS_INFO("irq_gpio = %d, ven_gpio = %d, dwnld_gpio = %d, error:%d\n",
              gdata->hw_res.irq_gpio, gdata->hw_res.ven_gpio, gdata->hw_res.download_gpio,
              ret);
    return SUCCESS;
err_ven:
    gpio_free(gdata->hw_res.irq_gpio);
    TMS_ERR("Failed, ret = %d\n", ret);
    return ret;
}

static int guidev_gpio_configure(struct hw_resource hw_res)
{
    int ret;

    if (gpio_is_valid(hw_res.irq_gpio)) {
        ret = gpio_direction_input(hw_res.irq_gpio);

        if (ret < 0) {
            TMS_ERR("not able to set irq gpio as input\n");
            return -ERROR;
        }
    }

    if (gpio_is_valid(hw_res.ven_gpio)) {
        ret = gpio_direction_output(hw_res.ven_gpio, hw_res.ven_flag);

        if (ret < 0) {
            TMS_ERR("not able to set ven gpio as output\n");
            return -ERROR;
        }
    }

    if (gpio_is_valid(hw_res.download_gpio)) {
        ret = gpio_direction_output(hw_res.download_gpio, hw_res.download_flag);

        if (ret < 0) {
            TMS_ERR("not able to set dwnld gpio as output\n");
            return -ERROR;
        }
    }

    return SUCCESS;
}

static int tms_chip_identification(struct match_info info, uint8_t *buf)
{
    int ret, i;
    int cmp_byte = 0;

    if (info.pattern == TMS_FW) {
        cmp_byte = info.sum - TMS_FW_CMP_BYTE - info.check_sum;
    } else if (info.pattern == TMS_BL) {
        cmp_byte = info.sum - TMS_BL_CMP_BYTE - info.check_sum;
    } else {
        TMS_ERR("Chip identified fail\n");
        return -ERROR;
    }

    if (cmp_byte < 0) {
        TMS_ERR("invalid response\n");
        return -ERROR;
    }

    TMS_INFO("Compare major version is %02x\n", buf[cmp_byte]);
    for (i = 0; i < info.ver_num; i++) {
        if ((buf[cmp_byte] & TMS_VERSION_MASK) == info.major_ver[i]) {
            ret = SUCCESS;
            TMS_DEBUG("Version matched\n");
            break;
        }

        ret = -ERROR;
    }

    return ret;
}

static int tms_receive_process(struct guide_dev *gdata, struct match_info *info,
                               uint8_t *buf)
{
    int ret;
    int retry;
    int read_len = 0;

    retry = info->read_retry;

    do {
        msleep(5);

        if (gpio_get_value(gdata->hw_res.irq_gpio)) {
            read_len = TMS_CMD_HEAD_LEN;
            ret = i2c_master_recv(gdata->client, buf, read_len);

            if (ret == read_len) {
                info->sum = ret;
            } else {
                TMS_ERR("Receive head error[%d]\n", ret);
                return -ERROR;
            }

            read_len = buf[HEAD_PAYLOAD_BYTE];

            if (read_len > (MAX_CMD_LEN - info->check_sum)) {
                read_len = MAX_CMD_LEN - info->check_sum;
                TMS_WARN("Receive payload is more than buf max\n");
            }

            ret = i2c_master_recv(gdata->client, buf + info->sum,
                                  read_len + info->check_sum);

            if (ret == (read_len + info->check_sum)) {
                info->sum = info->sum + ret;
            } else {
                TMS_ERR("Receive payload error[%d]\n", ret);
                return -ERROR;
            }

            ret = SUCCESS;
            break;
        }

        TMS_ERR("detected interrupt again\n");
        ret = -ERROR;
        retry--;
    } while (retry > 0);

    if (ret != SUCCESS) {
        TMS_ERR("No response is received\n");
        return -ERROR;
    }

    tms_buffer_dump("Rx <-", buf, info->sum);
    return ret;
}

static int tms_check_chip_info(struct guide_dev *gdata, struct match_info info)
{
    int ret;
    int retry;
    uint8_t read_buf[MAX_CMD_LEN];

    retry = info.write_retry;

    if (info.pattern == TMS_FW) {
        guidev_hw_reset();
        if (!gdata->dlpin_flag) {
            TMS_DEBUG("TMS chip jump to FW\n");
            nfc_jump_fw(gdata->client, gdata->hw_res.irq_gpio);
        }
        TMS_DEBUG("TMS chip FW match check...\n");
    } else if (info.pattern == TMS_BL) {
        if (!gdata->tms->set_gpio) {
            TMS_ERR("gdata->tms->set_gpio is NULL");
            return -ERROR;
        }
        if (gdata->dlpin_flag) {
            gdata->tms->set_download(gdata->hw_res, ON);
        }
        gdata->tms->set_gpio(gdata->hw_res.download_gpio, ON, WAIT_TIME_NONE, WAIT_TIME_10000US);
        guidev_hw_reset();
        TMS_DEBUG("TMS chip BL match check...\n");
    } else {
        TMS_ERR("TMS chip info error\n");
        return -ERROR;
    }

    if (info.write_len > 0 && info.write_len < MAX_CMD_LEN) {
        tms_buffer_dump("Tx ->", info.cmd, info.write_len);
    } else {
        TMS_WARN("Send Command too loog, set length is %d\n", MAX_CMD_LEN);
        info.write_len = MAX_CMD_LEN;
    }

    while (retry > 0) {
        ret = i2c_master_send(gdata->client, info.cmd, info.write_len);

        if (ret == info.write_len) {
            ret = tms_receive_process(gdata, &info, read_buf);

            if (info.pattern == TMS_FW) {
                /*if nci payload > 1, do not read + check_sum*/
                info.check_sum = 0;
                ret = tms_receive_process(gdata, &info, read_buf);
            } else if (info.pattern == TMS_BL) {
                 if (gdata->dlpin_flag) {
                    gdata->tms->set_download(gdata->hw_res, OFF);
                }
                gdata->tms->set_gpio(gdata->hw_res.download_gpio, OFF, WAIT_TIME_NONE, WAIT_TIME_10000US);
                guidev_hw_reset();
            }

            break;
        }

        TMS_DEBUG("Send Command error[%d]\n", ret);
        retry--;
        msleep(10);
        ret = -ERROR;
    };

    if (ret != SUCCESS) {
        TMS_ERR("Command transceive failed\n");
        return -ERROR;
    }

    ret = tms_chip_identification(info, read_buf);
    return ret;
}

static int guidev_chip_match(struct guide_dev *gdata)
{
    int ret = -ERROR;
    int count = 0;
    struct match_info info_list[] = {
        {
            .type =        TMS_THN31,
            .pattern =     TMS_FW,
            .name =        "thn31",
            .write_len =   4,
            .write_retry = 3,
            .read_retry =  10,
            .check_sum =   1,
            .ver_num =     2,
            .major_ver =   {0xC0, 0xD0},
            .cmd =         {0x20, 0x00, 0x01, 0x00},
        },
        {
            .type =        TMS_THN31,
            .pattern =     TMS_BL,
            .name =        "thn31",
            .write_len =   9,
            .write_retry = 3,
            .read_retry =  10,
            .check_sum =   1,
            .ver_num =     1,
            .major_ver =   {0xB0},
            .cmd =         {0x5A, 0x00, 0x05, 0x00, 0x4a, 0x02, 0x00, 0x00, 0x17},
        },
        {
            .type =        SAMPLE_DEV_1,
            .pattern =     SAMPLE_MATCH_1,
            .name =        "sample_match_1",
            .write_len =   9,
            .write_retry = 3,
            .read_retry =  10,
            .check_sum =   1,
            .ver_num =     1,
            .major_ver =   {0xB0},
            .cmd =         {0x5A, 0x00, 0x05, 0x00, 0x4a, 0x02, 0x00, 0x00, 0x17},
        },
        {
            .type =        SAMPLE_DEV_2,
            .pattern =     SAMPLE_MATCH_2,
            .name =        "sample_match_2",
            .write_len =   9,
            .write_retry = 3,
            .read_retry =  10,
            .check_sum =   1,
            .ver_num =     1,
            .major_ver =   {0xB0},
            .cmd =         {0x5A, 0x00, 0x05, 0x00, 0x4a, 0x02, 0x00, 0x00, 0x17},
        },
    };

    for (count = 0; count < ARRAY_SIZE(info_list); count++) {
        TMS_DEBUG("Check chip info %d times\n", count + 1);
        if (info_list[count].type == TMS_THN31) {
            ret = tms_check_chip_info(gdata, info_list[count]);
        } else if (info_list[count].type == SAMPLE_DEV_1) {
            //ret = sample_dev_1_check_chip_info(gdata, info_list);
            ret = -ERROR;
        } else if (info_list[count].type == SAMPLE_DEV_2) {
            //ret = sample_dev_2_check_chip_info(gdata, info_list);
            ret = -ERROR;
        }

        if (ret == SUCCESS) {
            chip_type = info_list[count].type;
            gdata->tms->nfc_name = info_list[count].name;
            TMS_INFO("Matched chip is %s\n", gdata->tms->nfc_name);
            break;
        }
    }

    return ret;
}

static int guidev_dev_probe(struct i2c_client *client,
                            const struct i2c_device_id *id)
{
    int ret;
    TMS_INFO("Enter\n");
    /* step1 : alloc data */
    guidev = kzalloc(sizeof(struct guide_dev), GFP_KERNEL);

    if (guidev == NULL) {
        TMS_ERR("Guide info alloc memory error\n");
        return -ENOMEM;
    }

    /* step2 : binding tms common data */
    guidev->tms = tms_common_data_binding();

    if (guidev->tms == NULL) {
        TMS_ERR("Get tms common info  error\n");
        ret = -ENOMEM;
        goto err_free_guidev;
    }

    /* step3 : init and binding parameters for easy operate */
    guidev->client        = client;
    guidev->dev           = &client->dev;

    /* step4 : dts parse */
    ret = guidev_parse_dts(guidev);

    if (ret) {
        TMS_ERR("failed to parse dts\n");
        goto err_free_guidev;
    }

    /* step5 : I2C function check */
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        TMS_ERR("need I2C_FUNC_I2C\n");
        guidev_resource_release(guidev);
        ret = -ENODEV;
        goto err_free_guidev;
    }

    /* step6 : Set gpio */
    ret = guidev_gpio_configure(guidev->hw_res);

    if (ret) {
        TMS_ERR("failed to configure\n");
        guidev_resource_release(guidev);
        goto err_free_guidev;
    }

    /* step7 : match nfc chip info */
    ret = guidev_chip_match(guidev);

    if (ret) {
        TMS_ERR("Fail to match chip\n");
    }

    /* step8 : Probe driver */
    ret = guidev_probe_driver(client, id, chip_type);

    if (ret) {
        TMS_ERR("failed to configure\n");
        goto err_free_guidev;
    }

    TMS_INFO("successfully\n");
    return SUCCESS;
err_free_guidev:
    kfree(guidev);
    guidev = NULL;
    TMS_ERR("Failed, ret = %d\n", ret);
    return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,1,0)
static int guidev_dev_remove(struct i2c_client *client)
#else
static void guidev_dev_remove(struct i2c_client *client)
#endif
{
    switch (chip_type) {
    case TMS_THN31:
        nfc_device_remove(client);
        break;

    case SAMPLE_DEV_1:
        //TMS_INFO("XXX XXX nfc start removed\n");
        //sample_dev_1_remove(client);
        break;

    case SAMPLE_DEV_2:
        //TMS_INFO("XXX XXX nfc start removed\n");
        //sample_dev_2_remove(client);
        break;

    default:
        TMS_ERR("No chip to remove %d\n", chip_type);
        break;
    }

    kfree(guidev);
    guidev = NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,1,0)
    return SUCCESS;
#endif
}

void guidev_dev_shutdown(struct i2c_client *client)
{
    if (!guidev || !guidev->tms || !guidev->tms->set_gpio) {
        TMS_ERR("set_gpio is NULL");
        return;
    }

    /* WAIT_TIME_20000US is hard reset guard time */
    guidev->tms->set_gpio(guidev->hw_res.ven_gpio, OFF, WAIT_TIME_NONE, WAIT_TIME_20000US);
}

static int guide_suspend(struct device *device)
{
    switch (chip_type) {
    case TMS_THN31:
        nfc_device_suspend(device);
        break;

    case SAMPLE_DEV_1:
        //TMS_INFO("XXX XXX nfc start suspend\n");
        //sample_dev_1_suspend(device);
        break;

    case SAMPLE_DEV_2:
        //TMS_INFO("XXX XXX nfc start suspend\n");
        //sample_dev_2_suspend(device);
        break;

    default:
        TMS_ERR("No chip to suspend %d\n", chip_type);
        break;
    }

    return SUCCESS;
}

static int guide_resume(struct device *device)
{
    switch (chip_type) {
    case TMS_THN31:
        nfc_device_resume(device);
        break;

    case SAMPLE_DEV_1:
        //TMS_INFO("XXX XXX nfc start resume\n");
        //sample_dev_1_resume(device);
        break;

    case SAMPLE_DEV_2:
        //TMS_INFO("XXX XXX nfc start resume\n");
        //sample_dev_2_suspend(device);
        break;

    default:
        TMS_ERR("No chip to resume %d\n", chip_type);
        break;
    }

    return SUCCESS;
}

static const struct i2c_device_id guide_dev_id[] = {
    {GUIDEDEV_NAME, 0 },
    { }
};

static struct of_device_id guide_match_table[] = {
    { .compatible = GUIDEDEV_NAME, },
    { }
};

static const struct dev_pm_ops guide_pm_ops = {
    SET_SYSTEM_SLEEP_PM_OPS(guide_suspend, guide_resume)
};

static struct i2c_driver tms_guide_driver = {
    .id_table = guide_dev_id,
    .probe  = guidev_dev_probe,
    .remove = guidev_dev_remove,
    .shutdown = guidev_dev_shutdown,
    .driver = {
        .name           = GUIDEDEV_NAME,
        .of_match_table = guide_match_table,
        .pm             = &guide_pm_ops,
        .probe_type     = PROBE_PREFER_ASYNCHRONOUS,
    },
};

int tms_guide_init(void)
{
    int ret = 0;
    TMS_INFO("Loading guide driver\n");
    ret = i2c_add_driver(&tms_guide_driver);

    if (ret != 0) {
        TMS_ERR("Add driver error ret = %d\n", ret);
    }

    return ret;
}

void tms_guide_exit(void)
{
    switch (chip_type) {
    case TMS_THN31:
#if IS_ENABLED(CONFIG_TMS_ESE_DEVICE)
        ese_driver_exit();
#endif
        break;

    case SAMPLE_DEV_1:
        //sample_dev_1_driver_exit(device);
        break;

    case SAMPLE_DEV_2:
        //sample_dev_2_driver_exit(device);
        break;

    default:
        TMS_ERR("No chip to resume %d\n", chip_type);
        break;
    }

    TMS_INFO("Unloading guide driver\n");
    i2c_del_driver(&tms_guide_driver);
}

MODULE_DESCRIPTION("TMS Guide Driver");
MODULE_LICENSE("GPL v2");
