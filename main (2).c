#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

#define WIFI_SSID       "gigi5g"
#define WIFI_PASS       "amora1234"

#define MAX_RETRY           10
#define MAX_SSID_LEN        32
#define SAFE_SSID_COUNT     5
#define WIFI_QUEUE_LENGTH   10
#define WDT_TIMEOUT_SECONDS 8

typedef struct {
    char ssid[MAX_SSID_LEN];
    int8_t rssi;
    uint64_t timestamp_us;
} wifi_status_t;

static const char *safe_ssids[SAFE_SSID_COUNT] = {
    "gigi5g",
    "REDE_SEGURA_1",
    "REDE_SEGURA_2",
    "REDE_GIOVANNA",
    "LAB_CORPORATIVO"
};

static SemaphoreHandle_t safe_list_mutex = NULL;
static QueueHandle_t wifi_event_queue = NULL;

static bool s_wifi_connected = false;
static int s_retry_num = 0;


static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        printf("[WIFI] Conectando...\n");
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        if (s_retry_num < MAX_RETRY) {
            s_retry_num++;
            printf("[WIFI] Reconectando (%d)...\n", s_retry_num);
            esp_wifi_connect();
        } else {
            printf("[WIFI] Falha após várias tentativas.\n");
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        s_retry_num = 0;
        printf("[WIFI] Conectado e IP obtido.\n");
    }
}


static void wifi_init_sta(void)
{
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = { 0 };
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}


static bool is_ssid_safe(const char *ssid)
{
    bool result = false;

    if (xSemaphoreTake(safe_list_mutex, pdMS_TO_TICKS(300)) == pdTRUE) {
        for (int i = 0; i < SAFE_SSID_COUNT; i++) {
            if (strcmp(ssid, safe_ssids[i]) == 0)
                result = true;
        }
        xSemaphoreGive(safe_list_mutex);
    }

    return result;
}


static void wifi_monitor_task(void *pv)
{
    wifi_ap_record_t ap;
    wifi_status_t st;

    while (1) {
        if (s_wifi_connected) {
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                memset(&st, 0, sizeof(st));
                strncpy(st.ssid, (char*)ap.ssid, MAX_SSID_LEN - 1);
                st.rssi = ap.rssi;
                st.timestamp_us = esp_timer_get_time();
                xQueueSend(wifi_event_queue, &st, 0);

                printf("[MONITOR] SSID=%s | RSSI=%d\n", st.ssid, st.rssi);
            }
        } else {
            printf("[MONITOR] Sem conexão...\n");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}


static void security_checker_task(void *pv)
{
    wifi_status_t rx;

    while (1) {
        if (xQueueReceive(wifi_event_queue, &rx, pdMS_TO_TICKS(6000))) {
            if (is_ssid_safe(rx.ssid)) {
                printf("[SECURITY] Rede segura: %s\n", rx.ssid);
            } else {
                printf("[SECURITY] ALERTA! Rede NÃO autorizada: %s\n", rx.ssid);
                printf("[SECURITY] Supervisor Giovanna: risco detectado.\n");
            }
        } else {
            printf("[SECURITY] Fila sem dados.\n");
        }
    }
}


static void heartbeat_task(void *pv)
{
    esp_task_wdt_add(NULL);

    int hb = 0;

    while (1) {
        hb++;
        printf("[HEARTBEAT] HB=%d | Supervisor Giovanna monitorando\n", hb);
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}


void app_main(void)
{
    printf("==== Sistema iniciado ====\n");
    printf("Responsável: Supervisor Giovanna\n");

    wifi_event_queue = xQueueCreate(WIFI_QUEUE_LENGTH, sizeof(wifi_status_t));
    safe_list_mutex = xSemaphoreCreateMutex();

    wifi_init_sta();

    xTaskCreate(security_checker_task, "security_checker_task", 4096, NULL, 3, NULL);
    xTaskCreate(wifi_monitor_task, "wifi_monitor_task", 4096, NULL, 2, NULL);
    xTaskCreate(heartbeat_task, "heartbeat_task", 4096, NULL, 1, NULL);

    while (1) vTaskDelay(portMAX_DELAY);
}
