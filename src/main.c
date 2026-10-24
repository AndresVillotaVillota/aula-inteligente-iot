#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

// ================= PINES =================

#define I2C_PORT        I2C_NUM_0
#define BH_SDA          GPIO_NUM_21
#define BH_SCL          GPIO_NUM_22
#define BH_ADDR         0x23

#define DHT_PIN         GPIO_NUM_27
#define PIR_PIN         GPIO_NUM_26

#define LED_PIN         GPIO_NUM_18
#define FAN_PIN         GPIO_NUM_19

// GPIO34 = ADC1 canal 6 en ESP32 clasico.
#define LM35_CHANNEL    ADC_CHANNEL_6
#define LM35_ATTEN      ADC_ATTEN_DB_6

// ================= UMBRALES =================

// Encender inicialmente con menos de 100 lux.
// Apagar siempre con 150 lux o mas.
#define LUX_ON          100.0f
#define LUX_OFF         150.0f

// Temperatura del LM35 para controlar el ventilador.
#define FAN_ON_C        35.0f
#define FAN_OFF_C       28.0f

// Mantener el LED encendido 30 segundos desde
// la ultima lectura activa del PIR, salvo mucha luz.
#define LED_HOLD_US     30000000LL

// Intervalo entre lecturas ambientales.
#define SAMPLE_TIME_US  2500000LL

// Tiempo inicial de estabilizacion del PIR.
#define PIR_WARMUP_US   60000000LL

static const char *TAG = "CONTROL";

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t adc_calibration;

static portMUX_TYPE dht_mux = portMUX_INITIALIZER_UNLOCKED;

// ================= SALIDAS Y ENTRADAS =================

static void gpio_init(void)
{
    ESP_ERROR_CHECK(gpio_reset_pin(LED_PIN));
    ESP_ERROR_CHECK(gpio_reset_pin(FAN_PIN));

    ESP_ERROR_CHECK(gpio_set_level(LED_PIN, 0));
    ESP_ERROR_CHECK(gpio_set_level(FAN_PIN, 0));

    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << LED_PIN) |
                        (1ULL << FAN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&outputs));

    gpio_config_t pir = {
        .pin_bit_mask = 1ULL << PIR_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&pir));

    gpio_config_t dht = {
        .pin_bit_mask = 1ULL << DHT_PIN,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&dht));
    ESP_ERROR_CHECK(gpio_set_level(DHT_PIN, 1));
}

// ================= BH1750 =================

static void bh1750_bus_init(void)
{
    i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BH_SDA,
        .scl_io_num = BH_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
        .clk_flags = 0
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &config));

    ESP_ERROR_CHECK(i2c_driver_install(
        I2C_PORT,
        I2C_MODE_MASTER,
        0,
        0,
        0
    ));
}

static esp_err_t bh_command(uint8_t command)
{
    return i2c_master_write_to_device(
        I2C_PORT,
        BH_ADDR,
        &command,
        1,
        pdMS_TO_TICKS(100)
    );
}

static esp_err_t bh1750_start(void)
{
    esp_err_t err = bh_command(0x01);

    if (err != ESP_OK) {
        return err;
    }

    // Medicion continua de alta resolucion.
    return bh_command(0x10);
}

static esp_err_t bh1750_read(float *lux)
{
    uint8_t data[2];

    esp_err_t err = i2c_master_read_from_device(
        I2C_PORT,
        BH_ADDR,
        data,
        sizeof(data),
        pdMS_TO_TICKS(100)
    );

    if (err == ESP_OK) {
        uint16_t raw =
            ((uint16_t)data[0] << 8) | data[1];

        *lux = raw / 1.2f;
    }

    return err;
}

// ================= DHT22 =================

static int dht_wait_while(int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();

    while (gpio_get_level(DHT_PIN) == level) {
        if (esp_timer_get_time() - start >= timeout_us) {
            return -1;
        }
    }

    return (int)(esp_timer_get_time() - start);
}

static esp_err_t dht22_read(
    float *temperature,
    float *humidity)
{
    uint8_t data[5] = {0};
    esp_err_t result = ESP_OK;

    // Senal de inicio.
    gpio_set_level(DHT_PIN, 0);
    esp_rom_delay_us(1200);

    portENTER_CRITICAL(&dht_mux);

    // Liberar DATA para recibir la respuesta.
    gpio_set_level(DHT_PIN, 1);

    if (dht_wait_while(1, 120) < 0 ||
        dht_wait_while(0, 120) < 0 ||
        dht_wait_while(1, 120) < 0) {
        result = ESP_ERR_TIMEOUT;
        goto finish;
    }

    // Recibir 40 bits.
    for (int bit = 0; bit < 40; bit++) {
        if (dht_wait_while(0, 100) < 0) {
            result = ESP_ERR_TIMEOUT;
            goto finish;
        }

        int high_us = dht_wait_while(1, 100);

        if (high_us < 0) {
            result = ESP_ERR_TIMEOUT;
            goto finish;
        }

        data[bit / 8] <<= 1;

        if (high_us > 40) {
            data[bit / 8] |= 1;
        }
    }

finish:
    portEXIT_CRITICAL(&dht_mux);
    gpio_set_level(DHT_PIN, 1);

    if (result != ESP_OK) {
        return result;
    }

    uint8_t checksum =
        (uint8_t)(data[0] + data[1] + data[2] + data[3]);

    if (checksum != data[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw_h =
        ((uint16_t)data[0] << 8) | data[1];

    uint16_t raw_t =
        ((uint16_t)(data[2] & 0x7F) << 8) | data[3];

    float t = raw_t / 10.0f;
    float h = raw_h / 10.0f;

    if (data[2] & 0x80) {
        t = -t;
    }

    if (h > 100.0f || t < -40.0f || t > 80.0f) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *temperature = t;
    *humidity = h;

    return ESP_OK;
}

// ================= LM35 =================

static void lm35_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(
        &unit,
        &adc_handle
    ));

    adc_oneshot_chan_cfg_t channel = {
        .atten = LM35_ATTEN,
        .bitwidth = ADC_BITWIDTH_12
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        adc_handle,
        LM35_CHANNEL,
        &channel
    ));

    adc_cali_line_fitting_config_t calibration = {
        .unit_id = ADC_UNIT_1,
        .atten = LM35_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
        .default_vref = 1100
    };

    ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(
        &calibration,
        &adc_calibration
    ));
}

static esp_err_t lm35_read(float *temperature)
{
    int sum_mv = 0;

    for (int i = 0; i < 64; i++) {
        int raw;
        int mv;

        esp_err_t err = adc_oneshot_read(
            adc_handle,
            LM35_CHANNEL,
            &raw
        );

        if (err != ESP_OK) {
            return err;
        }

        err = adc_cali_raw_to_voltage(
            adc_calibration,
            raw,
            &mv
        );

        if (err != ESP_OK) {
            return err;
        }

        sum_mv += mv;
    }

    // Promedio de 64 lecturas.
    // LM35: 10 mV por grado Celsius.
    *temperature = ((float)sum_mv / 64.0f) / 10.0f;

    return ESP_OK;
}

// ================= CONTROL =================

static void control_task(void *arg)
{
    bool led_on = false;
    bool fan_on = false;
    bool light_valid = false;
    bool pir_ready_announced = false;

    float lux = 0.0f;
    float lm35_temperature = 0.0f;

    int previous_pir = -1;

    // Instante hasta el que debe permanecer encendido.
    int64_t led_until_us = 0;

    int64_t start_us = esp_timer_get_time();
    int64_t next_sample_us = start_us + SAMPLE_TIME_US;

    bool bh_ready = (bh1750_start() == ESP_OK);

    ESP_LOGI(TAG, "Sistema iniciado");
    ESP_LOGI(TAG, "PIR: estabilizacion durante 60 segundos");

    while (1) {
        int64_t now = esp_timer_get_time();

        // Lecturas ambientales cada 2.5 segundos o mas.
        if (now >= next_sample_us) {
            esp_err_t err;

            // Reintentar iniciar el BH1750 si fallo.
            if (!bh_ready) {
                err = bh1750_start();

                if (err == ESP_OK) {
                    bh_ready = true;
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
            }

            err = bh_ready
                ? bh1750_read(&lux)
                : ESP_FAIL;

            light_valid = (err == ESP_OK);

            if (light_valid) {
                ESP_LOGI(
                    TAG,
                    "BH1750: %.1f lux",
                    lux
                );
            } else {
                bh_ready = false;

                ESP_LOGW(
                    TAG,
                    "BH1750: fallo de lectura"
                );
            }

            // Temperatura que controla el ventilador.
            err = lm35_read(&lm35_temperature);

            bool requested_fan = fan_on;

            if (err == ESP_OK) {
                if (lm35_temperature >= FAN_ON_C) {
                    requested_fan = true;
                } else if (lm35_temperature <= FAN_OFF_C) {
                    requested_fan = false;
                }

                ESP_LOGI(
                    TAG,
                    "LM35: %.1f C",
                    lm35_temperature
                );
            } else {
                // Mantener ventilacion si falla la lectura ADC.
                requested_fan = true;

                ESP_LOGE(
                    TAG,
                    "LM35: error ADC; ventilador encendido"
                );
            }

            if (requested_fan != fan_on) {
                fan_on = requested_fan;

                gpio_set_level(FAN_PIN, fan_on);

                ESP_LOGI(
                    TAG,
                    "VENTILADOR: %s",
                    fan_on ? "ENCENDIDO" : "APAGADO"
                );
            }

            // DHT22: lectura informativa.
            float dht_temperature;
            float humidity;

            err = dht22_read(
                &dht_temperature,
                &humidity
            );

            if (err == ESP_OK) {
                ESP_LOGI(
                    TAG,
                    "DHT22: %.1f C | Humedad: %.1f %%",
                    dht_temperature,
                    humidity
                );
            } else {
                ESP_LOGW(
                    TAG,
                    "DHT22: %s",
                    esp_err_to_name(err)
                );
            }

            next_sample_us =
                esp_timer_get_time() + SAMPLE_TIME_US;
        }

        // Revisar el PIR entre lecturas ambientales.
        bool pir_ready =
            (esp_timer_get_time() - start_us >= PIR_WARMUP_US);

        if (pir_ready && !pir_ready_announced) {
            ESP_LOGI(TAG, "PIR listo");
            pir_ready_announced = true;
        }

        int movement = pir_ready
            ? gpio_get_level(PIR_PIN)
            : 0;

        if (pir_ready && movement != previous_pir) {
            ESP_LOGI(
                TAG,
                "PIR: %s",
                movement ? "MOVIMIENTO" : "SALIDA INACTIVA"
            );

            previous_pir = movement;
        }

        // ========== LED: LUZ + MOVIMIENTO + TEMPORIZADOR ==========

        int64_t led_now_us = esp_timer_get_time();

        if (!light_valid || lux >= LUX_OFF) {
            // La iluminacion alta tiene prioridad.
            // Tambien apagar si falla la lectura de luz.
            led_until_us = 0;
        } else if (movement == 1 &&
                   (led_on || lux < LUX_ON)) {
            // Para encender desde apagado:
            // movimiento y menos de 100 lux.
            //
            // Si ya esta encendido:
            // renovar el tiempo mientras haya movimiento
            // y la iluminacion siga por debajo de 150 lux.
            led_until_us = led_now_us + LED_HOLD_US;
        }

        bool requested_led =
            light_valid &&
            (lux < LUX_OFF) &&
            (led_now_us < led_until_us);

        if (requested_led != led_on) {
            led_on = requested_led;

            gpio_set_level(LED_PIN, led_on);

            ESP_LOGI(
                TAG,
                "LED: %s",
                led_on ? "ENCENDIDO" : "APAGADO"
            );
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ================= PROGRAMA PRINCIPAL =================

void app_main(void)
{
    gpio_init();

    bh1750_bus_init();
    lm35_init();

    if (xTaskCreate(
            control_task,
            "Control_Task",
            4096,
            NULL,
            5,
            NULL) != pdPASS) {
        ESP_LOGE(
            TAG,
            "No se pudo crear la tarea"
        );

        abort();
    }
}