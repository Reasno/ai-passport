// components/bsp/src/bsp_battery.c
// 移植自 trae_card/components/platform/platform_esp32/src/battery_cw2017.c
#include "bsp_battery.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>

static const char *TAG = "bsp_batt";

#define CW_REG_VERSION       0x00   // 版本号,上电应答即代表芯片在位
#define CW_REG_VCELL_H       0x02   // 14bit 电压,V(uV) = raw * 312.5
#define CW_REG_SOC_H         0x04   // 高字节 = 整数百分比;低字节(0x05)= 1/256 %
#define CW_REG_CONFIG        0x08   // 0xF0=睡眠 / 0x30=复位态 / 0x00=正常
#define CW_REG_SOC_ALERT     0x0B   // bit7 = battery profile UPDATE_FLAG
#define CW_REG_PROFILE       0x10   // 80 字节 battery profile 起始地址

#define CW_CONFIG_RESTART    0x30
#define CW_CONFIG_SLEEP      0xF0
#define CW_CONFIG_NORMAL     0x00
#define CW_UPDATE_FLAG       0x80
#define CW_PROFILE_SIZE      80
#define CW_SOC_POLL_COUNT    50
#define CW_SOC_POLL_DELAY_MS 100

// FoloToy AI Passport 原厂优特利 520mAh 电芯 profile。
static const uint8_t s_battery_profile[CW_PROFILE_SIZE] = {
    0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAD, 0xC7, 0xC8, 0xCA, 0xBD, 0xB1, 0xC1, 0x94,
    0x88, 0xD1, 0xBD, 0x97, 0x88, 0x66, 0x56, 0x4A,
    0x3F, 0x33, 0x26, 0x5C, 0x37, 0xD1, 0x27, 0xD8,
    0xCC, 0xB7, 0xCF, 0xB3, 0xB2, 0xAE, 0xA6, 0x9E,
    0x99, 0x97, 0x9B, 0x86, 0x47, 0x1E, 0x17, 0x26,
    0x49, 0x96, 0xD9, 0xE1, 0xDD, 0xDC, 0xD4, 0x59,
    0x00, 0x00, 0x90, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5C,
};

static i2c_master_dev_handle_t s_dev;

static int cw_read(uint8_t reg, uint8_t *buf, size_t n)
{
    if (!s_dev) return -1;
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100) == ESP_OK ? 0 : -1;
}

static int cw_write(uint8_t reg, uint8_t val)
{
    if (!s_dev) return -1;
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, sizeof(b), 100) == ESP_OK ? 0 : -1;
}

static esp_err_t cw_restart(uint8_t target_mode)
{
    if (cw_write(CW_REG_CONFIG, CW_CONFIG_RESTART) != 0) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (cw_write(CW_REG_CONFIG, target_mode) != 0) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

static esp_err_t cw_profile_matches(bool *matches)
{
    uint8_t alert = 0;
    *matches = false;

    if (cw_read(CW_REG_SOC_ALERT, &alert, 1) != 0) return ESP_FAIL;
    if ((alert & CW_UPDATE_FLAG) == 0) return ESP_OK;

    for (size_t i = 0; i < CW_PROFILE_SIZE; ++i) {
        uint8_t value = 0;
        if (cw_read((uint8_t)(CW_REG_PROFILE + i), &value, 1) != 0) return ESP_FAIL;
        if (value != s_battery_profile[i]) return ESP_OK;
    }

    *matches = true;
    return ESP_OK;
}

static esp_err_t cw_update_profile(void)
{
    ESP_LOGI(TAG, "写入优特利 520mAh battery profile");

    esp_err_t err = cw_restart(CW_CONFIG_SLEEP);
    if (err != ESP_OK) return err;

    for (size_t i = 0; i < CW_PROFILE_SIZE; ++i) {
        if (cw_write((uint8_t)(CW_REG_PROFILE + i), s_battery_profile[i]) != 0) {
            ESP_LOGE(TAG, "battery profile 写入失败: offset=%u", (unsigned)i);
            return ESP_FAIL;
        }
    }

    for (size_t i = 0; i < CW_PROFILE_SIZE; ++i) {
        uint8_t value = 0;
        if (cw_read((uint8_t)(CW_REG_PROFILE + i), &value, 1) != 0 ||
            value != s_battery_profile[i]) {
            ESP_LOGE(TAG, "battery profile 校验失败: offset=%u", (unsigned)i);
            return ESP_FAIL;
        }
    }

    uint8_t alert = 0;
    if (cw_read(CW_REG_SOC_ALERT, &alert, 1) != 0 ||
        cw_write(CW_REG_SOC_ALERT, alert | CW_UPDATE_FLAG) != 0) {
        ESP_LOGE(TAG, "设置 battery profile UPDATE_FLAG 失败");
        return ESP_FAIL;
    }

    return cw_restart(CW_CONFIG_NORMAL);
}

static esp_err_t cw_wait_for_soc(void)
{
    for (int attempt = 0; attempt < CW_SOC_POLL_COUNT; ++attempt) {
        uint8_t soc = 0xFF;
        if (cw_read(CW_REG_SOC_H, &soc, 1) == 0 && soc <= 100) {
            ESP_LOGI(TAG, "CW2017 SOC 已就绪: %u%%", soc);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(CW_SOC_POLL_DELAY_MS));
    }

    ESP_LOGE(TAG, "CW2017 SOC 在 5 秒内未就绪");
    return ESP_ERR_TIMEOUT;
}

static void cw_remove_device(void)
{
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
}

esp_err_t bsp_battery_init(void)
{
    if (s_dev) return ESP_OK;

    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) return err;

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_I2C_CW2017_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(bsp_i2c_bus(), &dc, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "添加 I2C 设备失败: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t ver = 0;
    if (cw_read(CW_REG_VERSION, &ver, 1) != 0) {
        ESP_LOGW(TAG, "CW2017 未应答 —— 用 bsp_i2c_scan() 确认 0x%02X 是否在线;"
                      "无电量计的板子可忽略本项", BSP_I2C_CW2017_ADDR);
        cw_remove_device();
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "检测到 CW2017 VERSION=0x%02X", ver);

    bool profile_matches = false;
    err = cw_profile_matches(&profile_matches);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "读取 battery profile 或 UPDATE_FLAG 失败");
        cw_remove_device();
        return err;
    }

    if (profile_matches) {
        ESP_LOGI(TAG, "优特利 520mAh battery profile 已匹配");
        uint8_t config = 0;
        if (cw_read(CW_REG_CONFIG, &config, 1) != 0) {
            err = ESP_FAIL;
        } else if (config != CW_CONFIG_NORMAL) {
            err = cw_restart(CW_CONFIG_NORMAL);
        }
    } else {
        err = cw_update_profile();
    }
    if (err != ESP_OK) {
        cw_remove_device();
        return err;
    }

    err = cw_wait_for_soc();
    if (err != ESP_OK) {
        cw_remove_device();
        return err;
    }

    return ESP_OK;
}

int bsp_battery_soc(void)
{
    uint8_t b[2] = {0};
    if (cw_read(CW_REG_SOC_H, b, sizeof(b)) != 0) return -1;
    int soc = b[0];
    if (soc > 100 || soc == 0xFF) return -1;
    return soc;
}

int bsp_battery_mv(void)
{
    uint8_t b[2] = {0};
    if (cw_read(CW_REG_VCELL_H, b, sizeof(b)) != 0) return -1;
    uint32_t raw = ((uint32_t)b[0] << 8 | b[1]) & 0x3FFF;
    return (int)((raw * 3125) / 10000);
}
