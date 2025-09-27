#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "driver/ledc.h"
#include "esp_log.h"

// ----------- LDR / ADC -----------
#define LDR_CHANNEL         ADC1_CHANNEL_7      // GPIO35
#define ADC_WIDTH_SET       ADC_WIDTH_BIT_12
#define ADC_ATTEN_SET       ADC_ATTEN_DB_11     // ~0-3.3V
#define DEFAULT_VREF_MV     1100
#define NO_OF_SAMPLES       64                  // oversampling

// ----------- BUZZER (passive) via NPN on GPIO18 -----------
#define BUZZER_GPIO         GPIO_NUM_18
#define BUZZER_LEDC_MODE    LEDC_HIGH_SPEED_MODE
#define BUZZER_LEDC_TIMER   LEDC_TIMER_0
#define BUZZER_LEDC_CH      LEDC_CHANNEL_0
#define BUZZER_RES          LEDC_TIMER_10_BIT   // duty 0..1023
#define BUZZER_FREQ_HZ      2000                // 2 kHz tone (adjust to taste)
#define BUZZER_DUTY_ON      512                 // ~50% duty for loud tone

// ----------- Thresholds (hysteresis) -----------
#define THRESHOLD_ON_RAW    1000   // adc_raw BELOW this -> buzzer ON (dark)
#define THRESHOLD_OFF_RAW   1200   // adc_raw ABOVE this -> buzzer OFF (bright)

// Optional: beep instead of continuous tone
#define USE_BEEP_PATTERN    0      // 0=continuous while dark, 1=beep pattern
#define BEEP_ON_MS          200
#define BEEP_OFF_MS         300

static const char *TAG = "LDR_PASSIVE_BUZZER";
static esp_adc_cal_characteristics_t *adc_chars;

// ----------- ADC helpers -----------
static void adc_init(void) {
    adc1_config_width(ADC_WIDTH_SET);
    adc1_config_channel_atten(LDR_CHANNEL, ADC_ATTEN_SET);
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_SET, ADC_WIDTH_SET, DEFAULT_VREF_MV, adc_chars);
}

static inline uint32_t adc_read_avg(void) {
    uint32_t sum = 0;
    for (int i = 0; i < NO_OF_SAMPLES; i++) sum += adc1_get_raw((adc1_channel_t)LDR_CHANNEL);
    return sum / NO_OF_SAMPLES;
}

// ----------- Passive buzzer (PWM) -----------
static void buzzer_init(void) {
    ledc_timer_config_t tcfg = {
        .speed_mode       = BUZZER_LEDC_MODE,
        .duty_resolution  = BUZZER_RES,
        .timer_num        = BUZZER_LEDC_TIMER,
        .freq_hz          = BUZZER_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&tcfg);

    ledc_channel_config_t ccfg = {
        .gpio_num       = BUZZER_GPIO,
        .speed_mode     = BUZZER_LEDC_MODE,
        .channel        = BUZZER_LEDC_CH,
        .timer_sel      = BUZZER_LEDC_TIMER,
        .duty           = 0,    // start OFF
        .hpoint         = 0
    };
    ledc_channel_config(&ccfg);
}

static inline void buzzer_on(void) {
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CH, BUZZER_DUTY_ON);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CH);
}

static inline void buzzer_off(void) {
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CH, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CH);
}

// ----------- App -----------
void app_main(void) {
    adc_init();
    buzzer_init();

    ESP_LOGI(TAG, "LDR on GPIO35, Passive buzzer on GPIO18 (PWM %d Hz)", BUZZER_FREQ_HZ);
    ESP_LOGI(TAG, "Rule: adc_raw < %d -> BUZZER ON; > %d -> OFF", THRESHOLD_ON_RAW, THRESHOLD_OFF_RAW);

    bool alarm_on = false;

    while (1) {
        uint32_t raw = adc_read_avg();
        uint32_t mv  = esp_adc_cal_raw_to_voltage(raw, adc_chars);  // optional for logging

        // Hysteresis decision
        if (!alarm_on && raw < THRESHOLD_ON_RAW) {
            alarm_on = true;
        } else if (alarm_on && raw > THRESHOLD_OFF_RAW) {
            alarm_on = false;
        }

        if (!USE_BEEP_PATTERN) {
            if (alarm_on) buzzer_on(); else buzzer_off();
            ESP_LOGI(TAG, "ADC:%4u  V:%u.%03u V  Alarm:%s",
                     raw, mv/1000, mv%1000, alarm_on ? "ON" : "OFF");
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            // Beep pattern while dark
            if (alarm_on) {
                buzzer_on();
                vTaskDelay(pdMS_TO_TICKS(BEEP_ON_MS));
                buzzer_off();
                vTaskDelay(pdMS_TO_TICKS(BEEP_OFF_MS));
            } else {
                buzzer_off();
                vTaskDelay(pdMS_TO_TICKS(150));
            }
            ESP_LOGI(TAG, "ADC:%4u  V:%u.%03u V  Alarm:%s",
                     raw, mv/1000, mv%1000, alarm_on ? "BEEPING" : "OFF");
        }
    }
}