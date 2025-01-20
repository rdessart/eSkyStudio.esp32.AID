#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

int client_sock = -1;

#define TCP_PORT                   8766
#define UART_NUM                   UART_NUM_0
#define UART_TX_PIN                1
#define UART_RX_PIN                3
#define UART_BUFFER_SIZE           1024
#define WIFI_SSID                  "eSky_aircraft"
#define WIFI_PASS                  "eSky1234"
#define WIFI_CHANNEL               2
#define MAX_STA_CONN               4


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        printf("#AP;CONNECTION;%s;%d\n",event->mac, event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        printf("#AP;LEAVE;%s;%d\n",event->mac, event->aid);
    }
}

void wifi_init_softap(void)
{
    
    esp_netif_init();
    esp_event_loop_create_default();

    esp_netif_t *netif = esp_netif_create_default_wifi_ap();

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 254);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0); // Set netmask
    esp_netif_dhcps_stop(netif); // Stop DHCP server
    esp_netif_set_ip_info(netif, &ip_info); // Set static IP
    esp_netif_dhcps_start(netif); // Restart DHCP server

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&wifi_event_handler,NULL,NULL);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA3_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .pmf_cfg = {
                    .required = true,
            },
        },
    };

    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();
    printf("#OK;SSID:%s;password:%s;Channel:%d\n", WIFI_SSID, WIFI_PASS, WIFI_CHANNEL);
}

void tcp_server_task(void *pvParameters)
{
    char rx_buffer[128];
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Create socket
    int server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server_sock < 0) {
        printf("#ERROR;Unable to create socket;%d\n", errno);
        vTaskDelete(NULL);
        return;
    }

    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) 
    {
        printf("#ERROR;bind;%d\n", errno);
        close(server_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(server_sock, 1) < 0) 
    {
        printf("#ERROR;listen;%d\n", errno);
        close(server_sock);
        vTaskDelete(NULL);
        return;
    }

    printf("#OK;READY!\n");

    while (1) 
    {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) 
        {
            printf("#ERROR;accept;%d\n", errno);
            break;
        }
        printf("#CONNECTED;%d\n", client_sock);
        while (1) {
            int len = recv(client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if(len <= 0) break;

            rx_buffer[len] = '\0';
            printf("*%s", rx_buffer);
            // uart_write_bytes(UART_NUM, rx_buffer, len);
        }

        close(client_sock);
        client_sock = -1;
    }

    close(server_sock);
    vTaskDelete(NULL);
}

void uart_to_tcp_task(void *pvParameters) {
    uint8_t data[UART_BUFFER_SIZE];

    while (1) 
    {
        int len = uart_read_bytes(UART_NUM, data, UART_BUFFER_SIZE, 20 / portTICK_PERIOD_MS);
        if(len <= 0)
            continue;
        if(data[0] == '@')
        {
            printf("#REBOOT REQUESTED !\n");
            abort();
            continue;
        }
        if (client_sock >= 0) 
        {
            send(client_sock, data, len, 0);
        }
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      ret = nvs_flash_init();
    }

     uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_driver_install(UART_NUM, UART_BUFFER_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    wifi_init_softap();
    xTaskCreate(tcp_server_task, "tcp_server_task", 4096, NULL, 5, NULL);
    xTaskCreate(uart_to_tcp_task, "uart_to_tcp_task", 4096, NULL, 5, NULL);
}
