#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "addr_from_stdin.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "driver/i2s.h"

#if defined(CONFIG_EXAMPLE_IPV4)
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV4_ADDR
#elif defined(CONFIG_EXAMPLE_IPV6)
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV6_ADDR
#else
#define HOST_IP_ADDR ""
#endif

#define PORT CONFIG_EXAMPLE_PORT
#define SAMPLE_SIZE (16*8)

static const char *TAG = "data";
// static const char payload[] = "-5";

static const int i2s_num = 0; // i2s port number
static uint16_t i2s_readraw_buff[SAMPLE_SIZE];
size_t bytes_read;

static void tcp_client_task(void *pvParameters)
{
    // char rx_buffer[128];
    char host_ip[] = HOST_IP_ADDR;
    int addr_family = 0;
    int ip_protocol = 0;

    while (1)
    {
#if defined(CONFIG_EXAMPLE_IPV4)
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
#elif defined(CONFIG_EXAMPLE_IPV6)
        struct sockaddr_in6 dest_addr = {0};
        inet6_aton(host_ip, &dest_addr.sin6_addr);
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(PORT);
        dest_addr.sin6_scope_id = esp_netif_get_netif_impl_index(EXAMPLE_INTERFACE);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_IPV6;
#elif defined(CONFIG_EXAMPLE_SOCKET_IP_INPUT_STDIN)
        struct sockaddr_storage dest_addr = {0};
        ESP_ERROR_CHECK(get_addr_from_stdin(PORT, SOCK_STREAM, &ip_protocol, &addr_family, &dest_addr));
#endif
        int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, PORT);

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        if (err != 0)
        {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Successfully connected");

        while (1)
        {

            i2s_read(i2s_num, (char *)i2s_readraw_buff, SAMPLE_SIZE, &bytes_read, 100);
            //printf("%d", sizeof(i2s_readraw_buff));

            int err = send(sock, i2s_readraw_buff, SAMPLE_SIZE, 0);

            if (err < 0)
            {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                break;
            }
            /*
            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            // Error occurred during receiving
            if (len < 0)
            {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
                break;
            }
            // Data received
            else
            {
                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, host_ip);
                ESP_LOGI(TAG, "%s", rx_buffer);
            }
            */

            // vTaskDelay(12000 / portTICK_PERIOD_MS);
        }

        if (sock != -1)
        {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count = 8,
        .dma_buf_len = 128, // 64
        .use_apll = false,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2 // Interrupt level 1, default 0
    };

    static const i2s_pin_config_t pin_config = {
        .bck_io_num = 3,
        .ws_io_num = 4,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = 5};

    ESP_ERROR_CHECK(i2s_driver_install(i2s_num, &i2s_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(i2s_num, &pin_config));
    ESP_ERROR_CHECK(i2s_set_clk(i2s_num, 44100, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO));
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
    
    xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);
}