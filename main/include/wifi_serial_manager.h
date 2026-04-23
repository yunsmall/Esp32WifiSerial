#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <atomic>

#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace usbipdcpp {
    class UsbDevice;
    class StringPool;
    class SimpleVirtualDeviceHandler;
}

// 引脚验证函数类型，返回true表示可用
using PinValidator = std::function<bool(int pin)>;

class ConfigSerialCommunicationInterfaceHandler;
class ConfigSerialDataInterfaceHandler;
class TransparentSerialCommunicationInterfaceHandler;
class TransparentSerialDataInterfaceHandler;

class WifiSerialManager {
public:
    // 共享状态
    int tx_pin = 4;
    int rx_pin = 5;
    int rts_pin = -1;  // UART RTS输出引脚，-1表示禁用
    int cts_pin = -1;  // UART CTS输入引脚，-1表示禁用
    int baud_rate = 115200;
    int data_bits = 8;
    int stop_bits = 1;
    int parity = 0;
    bool flow_control = false;  // 是否启用硬件流控
    std::mutex mutex;

    uart_port_t uart_port = UART_NUM_1;
    std::atomic_bool uart_initialized{false};
    QueueHandle_t uart_event_queue = nullptr;

    // 引脚验证器
    PinValidator tx_validator;
    PinValidator rx_validator;
    PinValidator rts_validator;
    PinValidator cts_validator;

    // handler
    std::shared_ptr<ConfigSerialCommunicationInterfaceHandler> config_comm_handler;
    std::shared_ptr<ConfigSerialDataInterfaceHandler> config_data_handler;
    std::shared_ptr<TransparentSerialCommunicationInterfaceHandler> transparent_comm_handler;
    std::shared_ptr<TransparentSerialDataInterfaceHandler> transparent_data_handler;

    // device handler
    std::shared_ptr<usbipdcpp::SimpleVirtualDeviceHandler> config_device_handler;
    std::shared_ptr<usbipdcpp::SimpleVirtualDeviceHandler> transparent_device_handler;

    WifiSerialManager(PinValidator tx_val = nullptr, PinValidator rx_val = nullptr);

    void init_uart();
    std::shared_ptr<usbipdcpp::UsbDevice> create_config_device(usbipdcpp::StringPool &sp);
    std::shared_ptr<usbipdcpp::UsbDevice> create_transparent_device(usbipdcpp::StringPool &sp);
    int send_data(const uint8_t *data, size_t len);
    void reconfigure_uart();
    void reconfigure_pins();

private:
    int current_tx_ = -1;
    int current_rx_ = -1;
};
