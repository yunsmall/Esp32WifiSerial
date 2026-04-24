#include "wifi_serial_manager.h"
#include "config_serial.h"
#include "transparent_serial.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <virtual_device/CdcAcmConstants.h>

#include <virtual_device/SimpleVirtualDeviceHandler.h>
#include <Endpoint.h>

static const char *TAG = "WifiSerial";

// GPIO中断队列
static QueueHandle_t gpio_event_queue = nullptr;
static WifiSerialManager *gpio_manager_instance = nullptr;

// GPIO中断服务程序
static void IRAM_ATTR gpio_isr_handler(void *arg) {
    if (gpio_event_queue) {
        int pin = (int) arg;
        xQueueSendFromISR(gpio_event_queue, &pin, nullptr);
    }
}

WifiSerialManager::WifiSerialManager(PinValidator tx_val, PinValidator rx_val) :
    tx_validator(std::move(tx_val))
    , rx_validator(std::move(rx_val)) {
}

void WifiSerialManager::init_uart() {
    std::lock_guard lock(mutex);

    uart_config_t cfg = {};
    cfg.baud_rate = baud_rate;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = uart_hw_flow_control ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    uart_driver_install(uart_port, 12 * 1024, 6 * 1024, 40, &uart_event_queue, 0);
    uart_param_config(uart_port, &cfg);
    uart_set_pin(uart_port, tx_pin, rx_pin,
                 uart_hw_flow_control ? rts_pin : UART_PIN_NO_CHANGE,
                 uart_hw_flow_control ? cts_pin : UART_PIN_NO_CHANGE);

    current_tx_ = tx_pin;
    current_rx_ = rx_pin;
    uart_initialized = true;

    ESP_LOGI(TAG, "UART init: TX=%d RX=%d RTS=%d CTS=%d baud=%d flow=%d",
             tx_pin, rx_pin, rts_pin, cts_pin, baud_rate, uart_hw_flow_control);
}

std::shared_ptr<usbipdcpp::UsbDevice> WifiSerialManager::create_config_device(usbipdcpp::StringPool &sp) {
    using namespace usbipdcpp;

    std::vector<UsbInterface> ifaces = {
            UsbInterface{
                    .interface_class = 0x02, .interface_subclass = 0x02, .interface_protocol = 0x01,
                    .endpoints = {
                            UsbEndpoint{.address = 0x83, .attributes = 0x03, .max_packet_size = 64, .interval = 16}}
            },
            UsbInterface{
                    .interface_class = 0x0A, .interface_subclass = 0x00, .interface_protocol = 0x00,
                    .endpoints = {
                            UsbEndpoint{.address = 0x81, .attributes = 0x02, .max_packet_size = 64, .interval = 0},
                            UsbEndpoint{.address = 0x02, .attributes = 0x02, .max_packet_size = 64, .interval = 0}
                    }
            }
    };

    config_comm_handler = ifaces[0].with_handler<ConfigSerialCommunicationInterfaceHandler>(sp, *this);
    config_data_handler = ifaces[1].with_handler<ConfigSerialDataInterfaceHandler>(sp, *this);
    config_data_handler->set_tx_buffer_capacity(4096); // 4KB for config serial
    config_comm_handler->set_data_handler(config_data_handler.get());
    config_data_handler->set_comm_handler(config_comm_handler.get());

    auto device = std::make_shared<UsbDevice>(UsbDevice{
            .path = "/serial/config", .busid = "1-1", .bus_num = 1, .dev_num = 1,
            .speed = static_cast<uint32_t>(UsbSpeed::Full),
            .vendor_id = 0x1234, .product_id = 0x0001, .device_bcd = 0x0100,
            .device_class = 0x02, .configuration_value = 1, .num_configurations = 1,
            .interfaces = ifaces,
            .ep0_in = UsbEndpoint::get_default_ep0_in(),
            .ep0_out = UsbEndpoint::get_default_ep0_out(),
    });
    config_device_handler = device->with_handler<SimpleVirtualDeviceHandler>(sp);
    config_device_handler->change_string_product(L"WiFi Serial Config");
    config_device_handler->change_string_serial(L"WIFI-SERIAL-CFG");

    return device;
}

std::shared_ptr<usbipdcpp::UsbDevice> WifiSerialManager::create_transparent_device(usbipdcpp::StringPool &sp) {
    using namespace usbipdcpp;

    std::vector<UsbInterface> ifaces = {
            UsbInterface{
                    .interface_class = 0x02, .interface_subclass = 0x02, .interface_protocol = 0x01,
                    .endpoints = {
                            UsbEndpoint{.address = 0x85, .attributes = 0x03, .max_packet_size = 64, .interval = 16}}
            },
            UsbInterface{
                    .interface_class = 0x0A, .interface_subclass = 0x00, .interface_protocol = 0x00,
                    .endpoints = {
                            UsbEndpoint{.address = 0x84, .attributes = 0x02, .max_packet_size = 64, .interval = 0},
                            UsbEndpoint{.address = 0x03, .attributes = 0x02, .max_packet_size = 64, .interval = 0}
                    }
            }
    };

    transparent_comm_handler = ifaces[0].with_handler<TransparentSerialCommunicationInterfaceHandler>(sp, *this);
    transparent_data_handler = ifaces[1].with_handler<TransparentSerialDataInterfaceHandler>(sp, *this);
    transparent_data_handler->set_tx_buffer_capacity(8 * 1024);
    transparent_comm_handler->set_data_handler(transparent_data_handler.get());
    transparent_data_handler->set_comm_handler(transparent_comm_handler.get());

    auto device = std::make_shared<UsbDevice>(UsbDevice{
            .path = "/serial/transparent", .busid = "1-2", .bus_num = 1, .dev_num = 2,
            .speed = static_cast<uint32_t>(UsbSpeed::Full),
            .vendor_id = 0x1234, .product_id = 0x0002, .device_bcd = 0x0100,
            .device_class = 0x02, .configuration_value = 1, .num_configurations = 1,
            .interfaces = ifaces,
            .ep0_in = UsbEndpoint::get_default_ep0_in(),
            .ep0_out = UsbEndpoint::get_default_ep0_out(),
    });
    transparent_device_handler = device->with_handler<SimpleVirtualDeviceHandler>(sp);
    transparent_device_handler->change_string_product(L"WiFi Serial Transparent");
    transparent_device_handler->change_string_serial(L"WIFI-SERIAL-TRANS");

    return device;
}

int WifiSerialManager::send_data(const uint8_t *data, size_t len) {
    if (!uart_initialized)
        return -1;
    std::lock_guard lock(mutex);
    return uart_write_bytes(uart_port, data, len);
}

void WifiSerialManager::reconfigure_uart() {
    if (!uart_initialized)
        return;

    std::lock_guard lock(mutex);
    uart_config_t cfg = {};
    cfg.baud_rate = baud_rate;
    cfg.data_bits = static_cast<uart_word_length_t>(data_bits - 5);
    cfg.parity = static_cast<uart_parity_t>(parity);
    cfg.stop_bits = static_cast<uart_stop_bits_t>(stop_bits);
    cfg.flow_ctrl = uart_hw_flow_control ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    uart_param_config(uart_port, &cfg);
    uart_set_pin(uart_port, tx_pin, rx_pin,
                 uart_hw_flow_control ? rts_pin : UART_PIN_NO_CHANGE,
                 uart_hw_flow_control ? cts_pin : UART_PIN_NO_CHANGE);
    ESP_LOGI(TAG, "UART reconfigured: baud=%d flow=%d", baud_rate, uart_hw_flow_control);
}

void WifiSerialManager::reconfigure_pins() {
    if (!uart_initialized)
        return;

    std::lock_guard lock(mutex);
    uart_wait_tx_done(uart_port, pdMS_TO_TICKS(1000));

    // 检查TX和RX是否相同
    if (tx_pin == rx_pin) {
        ESP_LOGW(TAG, "TX and RX cannot be the same pin");
        return;
    }

    // 重置旧引脚
    if (current_tx_ >= 0 && current_tx_ != tx_pin) {
        gpio_reset_pin(static_cast<gpio_num_t>(current_tx_));
    }
    if (current_rx_ >= 0 && current_rx_ != rx_pin) {
        gpio_reset_pin(static_cast<gpio_num_t>(current_rx_));
    }

    // 重置新引脚（确保干净状态）
    if (tx_pin != current_tx_) {
        gpio_reset_pin(static_cast<gpio_num_t>(tx_pin));
    }
    if (rx_pin != current_rx_) {
        gpio_reset_pin(static_cast<gpio_num_t>(rx_pin));
    }

    uart_set_pin(uart_port, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    current_tx_ = tx_pin;
    current_rx_ = rx_pin;
    ESP_LOGI(TAG, "Pins changed: TX=%d RX=%d", tx_pin, rx_pin);
}

void WifiSerialManager::init_gpio() {
    // 创建GPIO事件队列（如果还没创建）
    if (!gpio_event_queue) {
        gpio_event_queue = xQueueCreate(10, sizeof(int));
    }

    // 安装GPIO ISR服务（只需安装一次）
    static bool isr_service_installed = false;
    if (!isr_service_installed) {
        gpio_install_isr_service(0);
        isr_service_installed = true;
    }

    gpio_manager_instance = this;
}

void WifiSerialManager::reconfigure_gpio() {
    std::lock_guard lock(mutex);

    bool hw_flow = uart_hw_flow_control;

    // 移除旧的GPIO中断
    if (current_dsr_pin_ >= 0 && current_dsr_pin_ != dsr_pin) {
        gpio_isr_handler_remove(static_cast<gpio_num_t>(current_dsr_pin_));
        gpio_reset_pin(static_cast<gpio_num_t>(current_dsr_pin_));
    }
    if (current_dcd_pin_ >= 0 && current_dcd_pin_ != dcd_pin) {
        gpio_isr_handler_remove(static_cast<gpio_num_t>(current_dcd_pin_));
        gpio_reset_pin(static_cast<gpio_num_t>(current_dcd_pin_));
    }
    if (current_cts_pin_ >= 0 && current_cts_pin_ != cts_pin) {
        gpio_isr_handler_remove(static_cast<gpio_num_t>(current_cts_pin_));
        gpio_reset_pin(static_cast<gpio_num_t>(current_cts_pin_));
    }
    if (current_ri_pin_ >= 0 && current_ri_pin_ != ri_pin) {
        gpio_isr_handler_remove(static_cast<gpio_num_t>(current_ri_pin_));
        gpio_reset_pin(static_cast<gpio_num_t>(current_ri_pin_));
    }

    // 重置旧的输出引脚
    if (current_dtr_pin_ >= 0 && current_dtr_pin_ != dtr_pin) {
        gpio_reset_pin(static_cast<gpio_num_t>(current_dtr_pin_));
    }
    // RTS引脚：引脚变化或启用硬件流控时需要重置
    if (current_rts_gpio_pin_ >= 0 && (current_rts_gpio_pin_ != rts_pin || hw_flow)) {
        gpio_reset_pin(static_cast<gpio_num_t>(current_rts_gpio_pin_));
        current_rts_gpio_pin_ = -1;
    }

    // 配置DTR输出引脚
    if (dtr_pin >= 0) {
        gpio_reset_pin(static_cast<gpio_num_t>(dtr_pin));
        gpio_set_direction(static_cast<gpio_num_t>(dtr_pin), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(dtr_pin), 0); // 默认低电平
        current_dtr_pin_ = dtr_pin;
        ESP_LOGI(TAG, "DTR output pin configured: %d", dtr_pin);
    }
    else {
        current_dtr_pin_ = -1;
    }

    // 配置RTS输出引脚（仅当不使用UART硬件流控时）
    if (!hw_flow && rts_pin >= 0) {
        gpio_reset_pin(static_cast<gpio_num_t>(rts_pin));
        gpio_set_direction(static_cast<gpio_num_t>(rts_pin), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(rts_pin), 0); // 默认低电平
        current_rts_gpio_pin_ = rts_pin;
        ESP_LOGI(TAG, "RTS output pin (software) configured: %d", rts_pin);
    }
    else {
        current_rts_gpio_pin_ = -1;
    }

    // 配置CTS输入引脚（仅当不使用UART硬件流控时，作为GPIO输入）
    if (!hw_flow && cts_pin >= 0) {
        gpio_reset_pin(static_cast<gpio_num_t>(cts_pin));
        gpio_set_direction(static_cast<gpio_num_t>(cts_pin), GPIO_MODE_INPUT);
        gpio_set_intr_type(static_cast<gpio_num_t>(cts_pin), GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add(static_cast<gpio_num_t>(cts_pin), gpio_isr_handler, (void *) cts_pin);
        current_cts_pin_ = cts_pin;
        ESP_LOGI(TAG, "CTS input pin configured: %d", cts_pin);
    }
    else {
        current_cts_pin_ = -1;
    }

    // 配置DSR输入引脚（带中断）
    if (dsr_pin >= 0) {
        gpio_reset_pin(static_cast<gpio_num_t>(dsr_pin));
        gpio_set_direction(static_cast<gpio_num_t>(dsr_pin), GPIO_MODE_INPUT);
        gpio_set_intr_type(static_cast<gpio_num_t>(dsr_pin), GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add(static_cast<gpio_num_t>(dsr_pin), gpio_isr_handler, (void *) dsr_pin);
        current_dsr_pin_ = dsr_pin;
        ESP_LOGI(TAG, "DSR input pin configured: %d", dsr_pin);
    }
    else {
        current_dsr_pin_ = -1;
    }

    // 配置DCD输入引脚（带中断）
    if (dcd_pin >= 0) {
        gpio_reset_pin(static_cast<gpio_num_t>(dcd_pin));
        gpio_set_direction(static_cast<gpio_num_t>(dcd_pin), GPIO_MODE_INPUT);
        gpio_set_intr_type(static_cast<gpio_num_t>(dcd_pin), GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add(static_cast<gpio_num_t>(dcd_pin), gpio_isr_handler, (void *) dcd_pin);
        current_dcd_pin_ = dcd_pin;
        ESP_LOGI(TAG, "DCD input pin configured: %d", dcd_pin);
    }
    else {
        current_dcd_pin_ = -1;
    }

    // 配置RI输入引脚（带中断）
    if (ri_pin >= 0) {
        gpio_reset_pin(static_cast<gpio_num_t>(ri_pin));
        gpio_set_direction(static_cast<gpio_num_t>(ri_pin), GPIO_MODE_INPUT);
        gpio_set_intr_type(static_cast<gpio_num_t>(ri_pin), GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add(static_cast<gpio_num_t>(ri_pin), gpio_isr_handler, (void *) ri_pin);
        current_ri_pin_ = ri_pin;
        ESP_LOGI(TAG, "RI input pin configured: %d", ri_pin);
    }
    else {
        current_ri_pin_ = -1;
    }
}

void WifiSerialManager::reset_gpio() {
    // 移除GPIO中断处理
    if (current_dsr_pin_ >= 0) {
        gpio_isr_handler_remove(static_cast<gpio_num_t>(current_dsr_pin_));
        gpio_reset_pin(static_cast<gpio_num_t>(current_dsr_pin_));
    }
    if (current_dcd_pin_ >= 0) {
        gpio_isr_handler_remove(static_cast<gpio_num_t>(current_dcd_pin_));
        gpio_reset_pin(static_cast<gpio_num_t>(current_dcd_pin_));
    }
    if (current_cts_pin_ >= 0) {
        gpio_isr_handler_remove(static_cast<gpio_num_t>(current_cts_pin_));
        gpio_reset_pin(static_cast<gpio_num_t>(current_cts_pin_));
    }
    if (current_ri_pin_ >= 0) {
        gpio_isr_handler_remove(static_cast<gpio_num_t>(current_ri_pin_));
        gpio_reset_pin(static_cast<gpio_num_t>(current_ri_pin_));
    }
    // DTR/RTS是输出引脚，只需重置
    if (current_dtr_pin_ >= 0) {
        gpio_reset_pin(static_cast<gpio_num_t>(current_dtr_pin_));
    }
    if (current_rts_gpio_pin_ >= 0) {
        gpio_reset_pin(static_cast<gpio_num_t>(current_rts_gpio_pin_));
    }

    current_dtr_pin_ = -1;
    current_dsr_pin_ = -1;
    current_dcd_pin_ = -1;
    current_cts_pin_ = -1;
    current_ri_pin_ = -1;
    current_rts_gpio_pin_ = -1;
}

void WifiSerialManager::set_dtr_level(bool level) {
    if (current_dtr_pin_ >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(current_dtr_pin_), level ? 1 : 0);
    }
}

void WifiSerialManager::set_rts_level(bool level) {
    if (current_rts_gpio_pin_ >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(current_rts_gpio_pin_), level ? 1 : 0);
    }
}

void WifiSerialManager::start_gpio_monitor() {
    gpio_monitor_stop_ = false;
    gpio_monitor_thread_ = std::thread([this]() {
        int pin;
        for (;;) {
            if (xQueueReceive(gpio_event_queue, &pin, pdMS_TO_TICKS(100))) {
                if (gpio_monitor_stop_)
                    break;

                // 发送串行状态通知
                std::uint16_t serial_state = 0;

                // 读取CTS状态
                if (current_cts_pin_ >= 0) {
                    int level = gpio_get_level(static_cast<gpio_num_t>(current_cts_pin_));
                    if (level) {
                        serial_state |= static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::CTS);
                    }
                }

                // 读取DSR状态
                if (current_dsr_pin_ >= 0) {
                    int level = gpio_get_level(static_cast<gpio_num_t>(current_dsr_pin_));
                    if (level) {
                        serial_state |= static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::DSR);
                    }
                }

                // 读取DCD状态
                if (current_dcd_pin_ >= 0) {
                    int level = gpio_get_level(static_cast<gpio_num_t>(current_dcd_pin_));
                    if (level) {
                        serial_state |= static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::DCD);
                    }
                }

                // 读取RI状态
                if (current_ri_pin_ >= 0) {
                    int level = gpio_get_level(static_cast<gpio_num_t>(current_ri_pin_));
                    if (level) {
                        serial_state |= static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::Ring);
                    }
                }

                // 通知USB主机
                if (transparent_comm_handler) {
                    transparent_comm_handler->notify_serial_state(serial_state);
                }
            }
            else if (gpio_monitor_stop_) {
                break;
            }
        }
    });
}

void WifiSerialManager::stop_gpio_monitor() {
    gpio_monitor_stop_ = true;
    if (gpio_monitor_thread_.joinable()) {
        gpio_monitor_thread_.join();
    }
}
