#include "transparent_serial.h"
#include "wifi_serial_manager.h"

#include <esp_log.h>
#include <virtual_device/CdcAcmConstants.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

static const char *TAG = "TransparentSerial";

TransparentSerialCommunicationInterfaceHandler::TransparentSerialCommunicationInterfaceHandler(
    usbipdcpp::UsbInterface &iface, usbipdcpp::StringPool &sp, WifiSerialManager &mgr)
    : CdcAcmCommunicationInterfaceHandler(iface, sp), manager(mgr)
{
}

void TransparentSerialCommunicationInterfaceHandler::on_new_connection(
    usbipdcpp::Session &current_session, usbipdcpp::error_code &ec) {
    CdcAcmCommunicationInterfaceHandler::on_new_connection(current_session, ec);

    manager.init_gpio();
    manager.reconfigure_gpio();
    manager.start_gpio_monitor();
}

void TransparentSerialCommunicationInterfaceHandler::on_disconnection(usbipdcpp::error_code &ec) {
    manager.stop_gpio_monitor();
    manager.reset_gpio();

    CdcAcmCommunicationInterfaceHandler::on_disconnection(ec);
}

void TransparentSerialCommunicationInterfaceHandler::on_set_line_coding(const usbipdcpp::LineCoding &coding) {
    {
        std::lock_guard lock(manager.mutex);
        manager.baud_rate = coding.dwDTERate;
        manager.data_bits = coding.bDataBits;
        manager.stop_bits = coding.bCharFormat + 1;
        manager.parity = coding.bParityType;
    }

    ESP_LOGI(TAG, "Line coding: baud=%d, data_bits=%d, stop_bits=%d, parity=%d",
             coding.dwDTERate, coding.bDataBits, coding.bCharFormat, coding.bParityType);

    manager.reconfigure_uart();
}

void TransparentSerialCommunicationInterfaceHandler::on_set_control_line_state(const usbipdcpp::ControlSignalState &state) {
    ESP_LOGI(TAG, "Control line: DTR=%d, RTS=%d", state.dtr, state.rts);

    // 控制DTR输出引脚
    manager.set_dtr_level(state.dtr);

    // 控制RTS输出引脚（软件流控时）
    manager.set_rts_level(state.rts);

    // 发送串行状态通知
    if (state.dtr) {
        std::uint16_t serial_state = 0;

        // 读取CTS引脚状态
        if (manager.current_cts_pin_ >= 0) {
            int level = gpio_get_level(static_cast<gpio_num_t>(manager.current_cts_pin_));
            if (level) {
                serial_state |= static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::CTS);
            }
        }

        // 读取DSR引脚状态
        if (manager.current_dsr_pin_ >= 0) {
            int level = gpio_get_level(static_cast<gpio_num_t>(manager.current_dsr_pin_));
            if (level) {
                serial_state |= static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::DSR);
            }
        }

        // 读取DCD引脚状态
        if (manager.current_dcd_pin_ >= 0) {
            int level = gpio_get_level(static_cast<gpio_num_t>(manager.current_dcd_pin_));
            if (level) {
                serial_state |= static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::DCD);
            }
        }

        // 读取RI引脚状态
        if (manager.current_ri_pin_ >= 0) {
            int level = gpio_get_level(static_cast<gpio_num_t>(manager.current_ri_pin_));
            if (level) {
                serial_state |= static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::Ring);
            }
        }

        send_serial_state_notification(serial_state);
    }
}

void TransparentSerialCommunicationInterfaceHandler::notify_serial_state(std::uint16_t state) {
    send_serial_state_notification(state);
}

TransparentSerialDataInterfaceHandler::TransparentSerialDataInterfaceHandler(
    usbipdcpp::UsbInterface &iface, usbipdcpp::StringPool &sp, WifiSerialManager &mgr)
    : CdcAcmDataInterfaceHandler(iface, sp), manager(mgr)
{
}

void TransparentSerialDataInterfaceHandler::on_new_connection(usbipdcpp::Session &current_session, usbipdcpp::error_code &ec) {
    CdcAcmDataInterfaceHandler::on_new_connection(current_session, ec);
    should_immediately_stop = false;

    // 启动UART接收线程
    uart_receive_thread = std::thread([this]() {
        uart_event_t event;
        uint8_t data[256];
        for (;;) {
            if (xQueueReceive(manager.uart_event_queue, &event, pdMS_TO_TICKS(100))) {
                if (should_immediately_stop) break;

                switch (event.type) {
                case UART_DATA:
                    // 循环读取直到读完所有数据
                    {
                        size_t remaining = event.size;
                        while (remaining > 0) {
                            size_t to_read = remaining > sizeof(data) ? sizeof(data) : remaining;
                            int len = uart_read_bytes(manager.uart_port, data, to_read, pdMS_TO_TICKS(100));
                            if (len > 0) {
                                send_data_blocking(data, len);
                                remaining -= len;
                            } else {
                                break;
                            }
                        }
                    }
                    break;

                case UART_BREAK:
                    // Break信号
                    ESP_LOGW(TAG, "UART break detected");
                    if (manager.transparent_comm_handler) {
                        manager.transparent_comm_handler->notify_serial_state(
                            static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::Break));
                    }
                    break;

                case UART_PARITY_ERR:
                    // 奇偶校验错误
                    ESP_LOGW(TAG, "UART parity error");
                    if (manager.transparent_comm_handler) {
                        manager.transparent_comm_handler->notify_serial_state(
                            static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::ParityError));
                    }
                    break;

                case UART_FRAME_ERR:
                    // 帧错误
                    ESP_LOGW(TAG, "UART frame error");
                    if (manager.transparent_comm_handler) {
                        manager.transparent_comm_handler->notify_serial_state(
                            static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::FramingError));
                    }
                    break;

                case UART_FIFO_OVF:
                    // 缓冲区溢出
                    ESP_LOGW(TAG, "UART buffer overflow");
                    if (manager.transparent_comm_handler) {
                        manager.transparent_comm_handler->notify_serial_state(
                            static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::OverrunError));
                    }
                    // 清空缓冲区
                    uart_flush_input(manager.uart_port);
                    break;

                default:
                    break;
                }
            } else if (should_immediately_stop) {
                break;
            }
        }
    });
}

void TransparentSerialDataInterfaceHandler::on_disconnection(usbipdcpp::error_code &ec) {
    should_immediately_stop = true;

    if (uart_receive_thread.joinable()) {
        uart_receive_thread.join();
    }

    CdcAcmDataInterfaceHandler::on_disconnection(ec);
}

void TransparentSerialDataInterfaceHandler::on_data_received(usbipdcpp::data_type &&data) {
    if (should_immediately_stop || !manager.uart_initialized) return;

    manager.send_data(data.data(), data.size());
}
