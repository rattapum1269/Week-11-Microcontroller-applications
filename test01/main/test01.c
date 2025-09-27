#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "driver/ledc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"

// กำหนดค่าคงที่สำหรับการใช้งาน
#define LDR_PIN         ADC1_CHANNEL_7  // GPIO35 สำหรับ LDR
#define LED_PIN         18             // GPIO32 สำหรับ LED PWM
#define LED_CHANNEL     LEDC_CHANNEL_0  // ช่อง PWM ที่ใช้
#define LED_TIMER       LEDC_TIMER_0    // Timer สำหรับ PWM
#define LED_MODE        LEDC_LOW_SPEED_MODE
#define PWM_BITS        13              // ความละเอียด PWM 13 บิต (0-8191)
#define PWM_FREQ        5000            // ความถี่ PWM 5 KHz
#define SAMPLE_COUNT    64              // จำนวนตัวอย่างในการอ่าน ADC

static const char *TAG = "LDR_LED_CONTROL";
static esp_adc_cal_characteristics_t *adc_chars;

// ฟังก์ชันตั้งค่า LED PWM
static void setup_led(void) {
    // กำหนดค่า LED Timer
    ledc_timer_config_t led_timer = {
        .speed_mode = LED_MODE,
        .duty_resolution = PWM_BITS,
        .timer_num = LED_TIMER,
        .freq_hz = PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&led_timer);

    // กำหนดค่า LED Channel
    ledc_channel_config_t led_channel = {
        .gpio_num = LED_PIN,
        .speed_mode = LED_MODE,
        .channel = LED_CHANNEL,
        .timer_sel = LED_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&led_channel);
}

// ฟังก์ชันตั้งค่า ADC
static void setup_adc(void) {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(LDR_PIN, ADC_ATTEN_DB_11);
    
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(
        ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, adc_chars
    );

    // แสดงประเภทการปรับเทียบ
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGI(TAG, "ใช้การปรับเทียบแบบ Two Point");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGI(TAG, "ใช้การปรับเทียบแบบ eFuse Vref");
    } else {
        ESP_LOGI(TAG, "ใช้การปรับเทียบแบบ Default Vref");
    }
}

// ฟังก์ชันอ่านค่า LDR
static uint32_t read_ldr(void) {
    uint32_t adc_reading = 0;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        adc_reading += adc1_get_raw(LDR_PIN);
    }
    return adc_reading / SAMPLE_COUNT;
}

// ฟังก์ชันแปลงค่า LDR เป็นค่า PWM
static uint32_t convert_to_pwm(uint32_t ldr_value) {
    // คำนวณค่า PWM โดยกลับค่า LDR
    // เมื่อแสงมาก (LDR ต่ำ) -> LED สว่างมาก
    // เมื่อแสงน้อย (LDR สูง) -> LED สว่างน้อย
    uint32_t pwm_value = ((4095 - ldr_value) * ((1 << PWM_BITS) - 1)) / 4095;
    return pwm_value;
}

void app_main(void) {
    // ตั้งค่าระบบ
    setup_led();
    setup_adc();

    ESP_LOGI(TAG, "เริ่มต้นระบบควบคุมแสง LED ด้วย LDR");
    ESP_LOGI(TAG, "LDR Pin: GPIO35 (ADC1_CH7)");
    ESP_LOGI(TAG, "LED Pin: GPIO32 (PWM)");
    ESP_LOGI(TAG, "PWM: %d-bit, %dHz", PWM_BITS, PWM_FREQ);

    // วนลูปอ่านค่า LDR และควบคุม LED
    while (1) {
        // อ่านค่า LDR
        uint32_t ldr_value = read_ldr();
        
        // แปลงเป็นแรงดัน
        uint32_t voltage = esp_adc_cal_raw_to_voltage(ldr_value, adc_chars);
        
        // แปลงเป็นค่า PWM
        uint32_t pwm_duty = convert_to_pwm(ldr_value);
        
        // ตั้งค่า PWM duty cycle
        ledc_set_duty(LED_MODE, LED_CHANNEL, pwm_duty);
        ledc_update_duty(LED_MODE, LED_CHANNEL);

        // คำนวณเปอร์เซ็นต์แสง
        float light_percent = ((float)ldr_value / 4095.0) * 100.0;
        float led_percent = ((float)pwm_duty / ((1 << PWM_BITS) - 1)) * 100.0;
        
        // แสดงสถานะ
        ESP_LOGI(TAG, "LDR: %d (%.1f%%) | แรงดัน: %dmV | LED: %.1f%%", 
                 ldr_value, light_percent, voltage, led_percent);
        
        vTaskDelay(pdMS_TO_TICKS(100));  // หน่วงเวลา 100ms
    }
}
