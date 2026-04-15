#include "config_serial.h"
#include "wifi_serial_manager.h"

#include <esp_log.h>
#include <spdlog/spdlog.h>
#include <virtual_device/CdcAcmConstants.h>
#include <cstring>
#include <cstdlib>
#include <strings.h>

static const char *TAG = "ConfigSerial";

ConfigSerialCommunicationInterfaceHandler::ConfigSerialCommunicationInterfaceHandler(
    usbipdcpp::UsbInterface &iface, usbipdcpp::StringPool &sp, WifiSerialManager &mgr)
    : CdcAcmCommunicationInterfaceHandler(iface, sp), manager(mgr)
{
}

void ConfigSerialCommunicationInterfaceHandler::on_set_control_line_state(const usbipdcpp::ControlSignalState &state) {
    SPDLOG_INFO("Config serial - Control line: DTR={}, RTS={}", state.dtr, state.rts);
    if (state.dtr && get_data_handler()) {
        send_serial_state_notification(static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::DCD) |
                                       static_cast<std::uint16_t>(usbipdcpp::CdcAcmSerialState::DSR));
    }
}

ConfigSerialDataInterfaceHandler::ConfigSerialDataInterfaceHandler(
    usbipdcpp::UsbInterface &iface, usbipdcpp::StringPool &sp, WifiSerialManager &mgr)
    : CdcAcmDataInterfaceHandler(iface, sp), manager(mgr)
{
}

void ConfigSerialDataInterfaceHandler::on_data_received(usbipdcpp::data_type &&data) {
    if (should_immediately_stop) return;

    ESP_LOGI(TAG, "Received %d bytes: %.*s", (int)data.size(), (int)data.size(), (char *)data.data());

    // 追加到固定缓冲区，空间不足时丢弃多余数据
    size_t to_copy = std::min(data.size(), MAX_CMD_LEN - recv_len);
    memcpy(recv_buffer + recv_len, data.data(), to_copy);
    recv_len += to_copy;

    // 查找并处理完整命令
    char *p = recv_buffer;
    char *end = recv_buffer + recv_len;
    char *nl;
    while ((nl = static_cast<char *>(memchr(p, '\n', end - p))) != nullptr) {
        size_t cmd_len = nl - p;

        // 去掉\r
        if (cmd_len > 0 && p[cmd_len - 1] == '\r') {
            cmd_len--;
        }

        if (cmd_len > 0) {
            process_command(p, cmd_len);
        }
        p = nl + 1;  // 移到下一条命令开头
    }

    // 把剩余数据移到缓冲区开头
    size_t remaining = end - p;
    if (remaining > 0 && p != recv_buffer) {
        memmove(recv_buffer, p, remaining);
    }
    recv_len = remaining;
}

void ConfigSerialDataInterfaceHandler::process_command(const char *cmd, size_t len) {
    SPDLOG_INFO("Config command: {}", std::string_view(cmd, len));

    // 检查前缀并解析参数（不区分大小写）
    if (len > 7 && strncasecmp(cmd, "SET TX ", 7) == 0) {
        int pin = std::atoi(cmd + 7);

        // 检查是否与RX相同
        {
            std::lock_guard lock(manager.mutex);
            if (pin == manager.rx_pin) {
                send_data("ERR: TX cannot be same as RX\n");
                return;
            }
        }

        if (manager.tx_validator && !manager.tx_validator(pin)) {
            send_data("ERR\n");
            return;
        }

        {
            std::lock_guard lock(manager.mutex);
            manager.tx_pin = pin;
        }
        manager.reconfigure_pins();
        SPDLOG_INFO("TX pin set to {}", pin);
        send_data("OK\n");
    } else if (len > 7 && strncasecmp(cmd, "SET RX ", 7) == 0) {
        int pin = std::atoi(cmd + 7);

        // 检查是否与TX相同
        {
            std::lock_guard lock(manager.mutex);
            if (pin == manager.tx_pin) {
                send_data("ERR: RX cannot be same as TX\n");
                return;
            }
        }

        if (manager.rx_validator && !manager.rx_validator(pin)) {
            send_data("ERR\n");
            return;
        }

        {
            std::lock_guard lock(manager.mutex);
            manager.rx_pin = pin;
        }
        manager.reconfigure_pins();
        SPDLOG_INFO("RX pin set to {}", pin);
        send_data("OK\n");
    } else if (len == 3 && strncasecmp(cmd, "GET", 3) == 0) {
        std::lock_guard lock(manager.mutex);
        char resp[32];
        int resp_len = snprintf(resp, sizeof(resp), "TX:%d RX:%d\n", manager.tx_pin, manager.rx_pin);
        send_data(std::string_view(resp, resp_len));
    } else if (len == 4 && strncasecmp(cmd, "HELP", 4) == 0) {
        send_data("Commands:\n");
        send_data("  SET TX <pin> - Set TX pin\n");
        send_data("  SET RX <pin> - Set RX pin\n");
        send_data("  GET          - Get current pins\n");
        send_data("  HELP         - Show this help\n");
    } else {
        send_data("ERR\n");
    }
}

void ConfigSerialDataInterfaceHandler::on_new_connection(usbipdcpp::Session &current_session, usbipdcpp::error_code &ec) {
    CdcAcmDataInterfaceHandler::on_new_connection(current_session, ec);
    should_immediately_stop = false;
}

void ConfigSerialDataInterfaceHandler::on_disconnection(usbipdcpp::error_code &ec) {
    should_immediately_stop = true;
    CdcAcmDataInterfaceHandler::on_disconnection(ec);
}
