#include "sdkconfig.h"

#include <thread>
#include <chrono>
#include <semaphore>

#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_pthread.h>
#include <asio.hpp>
#include <spdlog/spdlog.h>

#include "wifi_serial_manager.h"

#include <Server.h>
#include <utils/StringPool.h>

static const char *TAG = "wifi_serial";
static constexpr std::uint16_t server_port = CONFIG_WIFI_SERIAL_PORT;

static std::binary_semaphore wifi_reconnect_semaphore{0};
static std::thread wifi_connect_thread;
static std::atomic_bool wifi_thread_should_stop = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        wifi_reconnect_semaphore.release();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void init_wifi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, &instance_got_ip));

    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid), CONFIG_WIFI_SERIAL_SSID, sizeof(wifi_config.sta.ssid));
    strncpy(reinterpret_cast<char *>(wifi_config.sta.password), CONFIG_WIFI_SERIAL_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    esp_pthread_cfg_t pthread_cfg = esp_pthread_get_default_config();
    pthread_cfg.prio = 10;
    pthread_cfg.thread_name = "wifi_reconnect";
    esp_pthread_set_cfg(&pthread_cfg);
    wifi_connect_thread = std::thread([]() {
        while (!wifi_thread_should_stop) {
            wifi_reconnect_semaphore.acquire();
            if (wifi_thread_should_stop) break;
            esp_wifi_connect();
        }
    });

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialized");
}

static int thread_main() {
    // 配置pthread默认栈大小
    esp_pthread_cfg_t pthread_cfg = esp_pthread_get_default_config();
    pthread_cfg.stack_size = 8 * 1024;  // 8KB栈
    esp_pthread_set_cfg(&pthread_cfg);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    spdlog::set_level(spdlog::level::info);

    init_wifi();

    // 创建管理器
    WifiSerialManager manager;
    manager.tx_pin = CONFIG_SERIAL_TX_PIN;
    manager.rx_pin = CONFIG_SERIAL_RX_PIN;
    manager.baud_rate = CONFIG_SERIAL_BAUD_RATE;

    // 初始化UART
    manager.init_uart();

    // 创建设备
    usbipdcpp::StringPool string_pool;
    auto config_dev = manager.create_config_device(string_pool);
    auto trans_dev = manager.create_transparent_device(string_pool);

    // 启动服务器
    usbipdcpp::Server server;
    server.set_before_thread_create_callback([](usbipdcpp::ThreadPurpose) {
        esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
        cfg.stack_size = 16 * 1024;
        esp_pthread_set_cfg(&cfg);
    });
    server.add_device(std::move(config_dev));
    server.add_device(std::move(trans_dev));

    asio::ip::tcp::endpoint endpoint{asio::ip::tcp::v4(), server_port};
    server.start(endpoint);

    SPDLOG_INFO("USBIP Server started on port {}", server_port);
    SPDLOG_INFO("Config serial: busid 1-1");
    SPDLOG_INFO("Transparent serial: busid 1-2");

    while (true) {
        ESP_LOGI(TAG, "Free heap: %lu, min free: %lu",
                 esp_get_free_heap_size(),
                 esp_get_minimum_free_heap_size());
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    server.stop();
    return 0;
}

extern "C" void app_main(void) {
    std::thread main_thread([]() {
        ESP_LOGI(TAG, "Starting WiFi Serial...");
        thread_main();
    });
    main_thread.join();
}
