#include "wifi_serial_manager.h"
#include "config_serial.h"
#include "transparent_serial.h"

#include <esp_log.h>
#include <driver/gpio.h>

#include <virtual_device/SimpleVirtualDeviceHandler.h>
#include <Endpoint.h>

static const char *TAG = "WifiSerial";

WifiSerialManager::WifiSerialManager(PinValidator tx_val, PinValidator rx_val)
    : tx_validator(std::move(tx_val))
    , rx_validator(std::move(rx_val))
{
}

void WifiSerialManager::init_uart() {
    std::lock_guard lock(mutex);

    uart_config_t cfg = {};
    cfg.baud_rate = baud_rate;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = flow_control ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    uart_driver_install(uart_port, 8192, 8192, 40, &uart_event_queue, 0);
    uart_param_config(uart_port, &cfg);
    uart_set_pin(uart_port, tx_pin, rx_pin,
                 flow_control ? rts_pin : UART_PIN_NO_CHANGE,
                 flow_control ? cts_pin : UART_PIN_NO_CHANGE);

    current_tx_ = tx_pin;
    current_rx_ = rx_pin;
    uart_initialized = true;

    ESP_LOGI(TAG, "UART init: TX=%d RX=%d RTS=%d CTS=%d baud=%d flow=%d",
             tx_pin, rx_pin, rts_pin, cts_pin, baud_rate, flow_control);
}

std::shared_ptr<usbipdcpp::UsbDevice> WifiSerialManager::create_config_device(usbipdcpp::StringPool &sp) {
    using namespace usbipdcpp;

    std::vector<UsbInterface> ifaces = {
        UsbInterface{
            .interface_class = 0x02, .interface_subclass = 0x02, .interface_protocol = 0x01,
            .endpoints = { UsbEndpoint{.address = 0x83, .attributes = 0x03, .max_packet_size = 64, .interval = 16} }
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
    config_data_handler->set_tx_buffer_capacity(4096);  // 4KB for config serial
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
            .endpoints = { UsbEndpoint{.address = 0x85, .attributes = 0x03, .max_packet_size = 64, .interval = 16} }
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
    transparent_data_handler->set_tx_buffer_capacity(8*1024);
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
    if (!uart_initialized) return -1;
    std::lock_guard lock(mutex);
    return uart_write_bytes(uart_port, data, len);
}

void WifiSerialManager::reconfigure_uart() {
    if (!uart_initialized) return;

    std::lock_guard lock(mutex);
    uart_config_t cfg = {};
    cfg.baud_rate = baud_rate;
    cfg.data_bits = static_cast<uart_word_length_t>(data_bits - 5);
    cfg.parity = static_cast<uart_parity_t>(parity);
    cfg.stop_bits = static_cast<uart_stop_bits_t>(stop_bits);
    cfg.flow_ctrl = flow_control ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    uart_param_config(uart_port, &cfg);
    uart_set_pin(uart_port, tx_pin, rx_pin,
                 flow_control ? rts_pin : UART_PIN_NO_CHANGE,
                 flow_control ? cts_pin : UART_PIN_NO_CHANGE);
    ESP_LOGI(TAG, "UART reconfigured: baud=%d flow=%d", baud_rate, flow_control);
}

void WifiSerialManager::reconfigure_pins() {
    if (!uart_initialized) return;

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
