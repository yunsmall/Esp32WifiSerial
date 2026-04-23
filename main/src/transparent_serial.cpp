#include "transparent_serial.h"
#include "wifi_serial_manager.h"

#include <esp_log.h>
#include <virtual_device/CdcAcmConstants.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

static const char *TAG = "TransparentSerial";

TransparentSerialCommunicationInterfaceHandler::TransparentSerialCommunicationInterfaceHandler(
    usbipdcpp::UsbInterface &iface, usbipdcpp::StringPool &sp, WifiSerialManager &mgr)
    : CdcAcmCommunicationInterfaceHandler(iface, sp), manager(mgr)
{
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
    if (state.dtr) {
        send_serial_state_notification(static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::DCD) |
                                       static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::DSR));
    }
}

TransparentSerialDataInterfaceHandler::TransparentSerialDataInterfaceHandler(
    usbipdcpp::UsbInterface &iface, usbipdcpp::StringPool &sp, WifiSerialManager &mgr)
    : CdcAcmDataInterfaceHandler(iface, sp), manager(mgr)
{
}

void TransparentSerialDataInterfaceHandler::on_new_connection(usbipdcpp::Session &current_session, usbipdcpp::error_code &ec) {
    CdcAcmDataInterfaceHandler::on_new_connection(current_session, ec);
    should_immediately_stop = false;
    host_ready_to_receive = false;  // 等待主机RTS信号

    uart_receive_thread = std::thread([this]() {
        uart_event_t event;
        uint8_t data[256];
        for (;;) {
            if (xQueueReceive(manager.uart_event_queue, &event, pdMS_TO_TICKS(100))) {
                if (should_immediately_stop) break;
                if (event.type == UART_DATA) {
                    // 循环读取直到读完所有数据
                    size_t remaining = event.size;
                    while (remaining > 0) {
                        size_t to_read = remaining > sizeof(data) ? sizeof(data) : remaining;
                        int len = uart_read_bytes(manager.uart_port, data, to_read, pdMS_TO_TICKS(100));
                        if (len > 0) {
                            // 快速路径：主机已准备好
                            if (!host_ready_to_receive) {
                                // 慢速路径：等待主机准备好
                                std::unique_lock<std::mutex> lock(host_rts_mutex_);
                                host_rts_cv_.wait(lock, [this] {
                                    return host_ready_to_receive.load() || should_immediately_stop.load();
                                });
                            }
                            if (should_immediately_stop) break;

                            send_data_blocking(data, len);
                            remaining -= len;
                        } else {
                            break;
                        }
                    }
                }
            } else if (should_immediately_stop) {
                break;
            }
        }
    });
}

void TransparentSerialDataInterfaceHandler::on_disconnection(usbipdcpp::error_code &ec) {
    should_immediately_stop = true;
    host_rts_cv_.notify_one();  // 唤醒等待的线程
    if (uart_receive_thread.joinable()) {
        uart_receive_thread.join();
    }
    CdcAcmDataInterfaceHandler::on_disconnection(ec);
}

void TransparentSerialDataInterfaceHandler::on_data_received(usbipdcpp::data_type &&data) {
    if (should_immediately_stop || !manager.uart_initialized) return;

    manager.send_data(data.data(), data.size());
}

void TransparentSerialDataInterfaceHandler::on_rts_changed(bool rts) {
    ESP_LOGI(TAG, "RTS changed: %d", rts);
    host_ready_to_receive = rts;
    if (rts) {
        host_rts_cv_.notify_one();  // 唤醒等待的发送线程
    }
}
