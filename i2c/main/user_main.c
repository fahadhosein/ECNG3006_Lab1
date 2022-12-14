#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"

#include "driver/i2c.h"


static const char *TAG = "main";

#define I2C_MASTER_SCL_IO   2   // GPIO number for I2C master clock
#define I2C_MASTER_SDA_IO   0   // GPIO number for I2C master data
#define I2C_MASTER_NUM I2C_NUM_0        // I2C port number for master dev
#define I2C_MASTER_TX_BUF_DISABLE   0   // I2C master do not need buffer
#define I2C_MASTER_RX_BUF_DISABLE   0   // I2C master do not need buffer

// ADS1115 Pin Addresses
#define ADS1115_GND 0x48
#define ADS1115_VDD 0x49
#define ADS1115_SDA 0x4A
#define ADS1115_SCL 0x4B

#define WRITE_BIT       I2C_MASTER_WRITE // I2C master write
#define READ_BIT        I2C_MASTER_READ  // I2C master read
#define ACK_CHECK_EN    0x1              // I2C master will check ack from slave
#define ACK_CHECK_DIS   0x0              // I2C master will not check ack from slave
#define ACK_VAL         0x0              // I2C ack value
#define NACK_VAL        0x1              // I2C nack value
#define LAST_NACK_VAL   0x2              // I2C last_nack value

// ADS1115 Register Addresses
#define ADS1115_CONV 0x00
#define ADS1115_CONFIG 0x01
#define ADS1115_LOTHRESH 0x02
#define ADS1115_HITHRESH 0x03

typedef struct conf
{
    uint8_t OS;             // Operational Status: 1-Bit
    uint8_t MUX;            // Input MUX: 3-Bits
    uint8_t PGA;            // Programmable Gain Amplifier: 3-Bits
    uint8_t MODE;           // Mode: 1-Bit
    uint8_t DR;             // Data Rate: 3-Bits
    uint8_t COMP_MODE;      // Comparator Mode: 1-Bit
    uint8_t COMP_POL;       // Comparator Polarity: 1-Bit
    uint8_t COMP_LAT;       // Latching Comparator: 1-Bit
    uint8_t COMP_QUE;       // Comparator Queue and Disable: 2-Bits
    uint16_t CONFIG;        // 16-bit Configuration
} ADS1115_CONF;

static esp_err_t i2c_master_init()
{
    int i2c_master_port = I2C_MASTER_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = 1;
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = 1;
    conf.clk_stretch_tick = 300; // 300 ticks, Clock stretch is about 210us, you can make changes according to the actual situation.
    ESP_ERROR_CHECK(i2c_driver_install(i2c_master_port, conf.mode));
    ESP_ERROR_CHECK(i2c_param_config(i2c_master_port, &conf));
    return ESP_OK;
}

static esp_err_t i2c_master_ads1115_write(i2c_port_t i2c_num, uint8_t reg_address, uint8_t *data, size_t data_len)
{
    int ret;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, ADS1115_GND << 1 | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg_address, ACK_CHECK_EN);
    i2c_master_write(cmd, data, data_len, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    return ret;
}

static esp_err_t i2c_master_ads1115_read(i2c_port_t i2c_num, uint8_t reg_address, uint8_t *data, size_t data_len)
{
    int ret;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, ADS1115_GND << 1 | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg_address, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) 
    {
        ESP_LOGI(TAG, "Unable to read data from ADS1115!\n");
        return ret;
    }

    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, ADS1115_GND << 1 | READ_BIT, ACK_CHECK_EN);
    i2c_master_read(cmd, data, data_len, LAST_NACK_VAL);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    return ret;
}

static esp_err_t data_write(i2c_port_t i2c_num, uint8_t reg_address, uint16_t data)
{
    int ret;
    uint8_t buf[2];
    buf[0] = (data >> 8) & 0xFF;
    buf[1] = (data >> 0) & 0xFF;

    ret = i2c_master_ads1115_write(i2c_num, reg_address, buf, 2);
    return ret;
}

static esp_err_t data_read(i2c_port_t i2c_num, uint8_t reg_address, uint16_t *data)
{
    int ret;
    uint8_t buf[2];

    ret = i2c_master_ads1115_read(i2c_num, reg_address, buf, 2);
    *data = (buf[0] << 8) | buf[1];
    return ret;
}

static esp_err_t i2c_master_ads1115_init(i2c_port_t i2c_num)
{
    vTaskDelay(100 / portTICK_RATE_MS);
    i2c_master_init();

    ADS1115_CONF conf;
    conf.OS = 0x00;        // NULL
    conf.MUX = 0x04;       // AINp = AIN0 and AINn = GND
    conf.PGA = 0x01;       // FS = 4.096 V
    conf.MODE = 0x00;      // Continuous-Conversion Mode
    conf.DR = 0x04;        // 128SPS
    conf.COMP_MODE = 0x00; // Traditional Comparator
    conf.COMP_POL = 0x00;  // Active Low
    conf.COMP_LAT = 0x00;  // Non-latching Comparator
    conf.COMP_QUE = 0x02;  // Assert After Four Conversions

    uint16_t data;
    data = (conf.OS << 3) | conf.MUX;
    data = (data << 3) | conf.PGA;
    data = (data << 1) | conf.MODE;
    data = (data << 3) | conf.DR;
    data = (data << 1) | conf.COMP_MODE;
    data = (data << 1) | conf.COMP_POL;
    data = (data << 1) | conf.COMP_LAT;
    data = (data << 2) | conf.COMP_QUE;
    conf.CONFIG = data;

    // Output Configuration Bits
    ESP_LOGI(TAG, "Configuration Bits: %d\n", (int)conf.CONFIG);

    // Writing to CONFIG Register
    ESP_ERROR_CHECK(data_write(i2c_num, ADS1115_CONFIG, conf.CONFIG));

    return ESP_OK;
}

static void i2c_task(void *arg)
{
    uint16_t data;
    double voltage;
    esp_err_t ret;

    ret  = i2c_master_ads1115_init(I2C_MASTER_NUM);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "ADS1115 Initialised!\n");
    }
    else
    {
        i2c_driver_delete(I2C_MASTER_NUM);
    }
    while(1)
    {
        ret = data_read(I2C_MASTER_NUM, ADS1115_CONV, &data);
        if(ret == ESP_OK)
        {
            ESP_LOGI(TAG, "ADS1115 Read!\n");
            voltage = (double)data * 1.25e-4;
            ESP_LOGI(TAG, "Voltage = %d.%d V\n", (uint16_t)voltage, (uint16_t)(voltage * 100) % 100);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
        else
        {
            ESP_LOGI(TAG, "Unable To Read ADS1115\n");
        }
    }


}

void app_main(void)
{
    //start I2C task
    xTaskCreate(i2c_task, "i2c_task", 2048, NULL, 10, NULL);
}