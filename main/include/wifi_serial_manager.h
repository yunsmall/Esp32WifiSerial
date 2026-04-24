#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

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
    int rts_pin = -1;  // UART RTS输出引脚（通知外部设备暂停），-1禁用
    int cts_pin = -1;  // UART CTS输入引脚（外部设备流控信号），-1禁用
    int dtr_pin = -1;  // DTR输出引脚（主机数据终端就绪），-1禁用
    int dsr_pin = -1;  // DSR输入引脚（外部设备就绪信号），-1禁用
    int dcd_pin = -1;  // DCD输入引脚（载波检测信号），-1禁用
    int ri_pin = -1;   // RI输入引脚（振铃指示信号），-1禁用
    int baud_rate = 115200;
    int data_bits = 8;
    int stop_bits = 1;
    int parity = 0;
    bool uart_hw_flow_control = false;  // 是否启用UART硬件流控(RTS/CTS)
    std::mutex mutex;

    uart_port_t uart_port = UART_NUM_1;
    std::atomic_bool uart_initialized{false};
    QueueHandle_t uart_event_queue = nullptr;

    // 引脚验证器
    PinValidator tx_validator;
    PinValidator rx_validator;
    PinValidator rts_validator;
    PinValidator cts_validator;
    PinValidator dtr_validator;
    PinValidator dsr_validator;
    PinValidator dcd_validator;
    PinValidator ri_validator;

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

    // GPIO控制方法
    void init_gpio();                    // 初始化GPIO ISR服务
    void reconfigure_gpio();             // 配置DTR/DSR/DCD/RTS GPIO
    void reset_gpio();                   // 重置所有GPIO引脚
    void set_dtr_level(bool level);      // 设置DTR电平
    void set_rts_level(bool level);      // 设置RTS电平（软件流控时）
    void start_gpio_monitor();           // 启动GPIO中断监测
    void stop_gpio_monitor();            // 停止GPIO中断监测

    // 当前生效的GPIO引脚（供handler读取）
    int current_dtr_pin_ = -1;
    int current_dsr_pin_ = -1;
    int current_dcd_pin_ = -1;
    int current_cts_pin_ = -1;   // 软件模式时的CTS输入引脚
    int current_ri_pin_ = -1;

private:
    int current_tx_ = -1;
    int current_rx_ = -1;
    int current_rts_gpio_pin_ = -1;  // 软件流控时的RTS GPIO引脚

    std::thread gpio_monitor_thread_;
    std::atomic_bool gpio_monitor_stop_{false};
};
