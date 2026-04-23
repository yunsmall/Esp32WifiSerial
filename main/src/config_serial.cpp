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
    usbipdcpp::UsbInterface &iface, usbipdcpp::StringPool &sp, WifiSerialManager &mgr) :
    CdcAcmDataInterfaceHandler(iface, sp), manager(mgr), recv_buffer{} {
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

    // 跳过前导空格
    while (len > 0 && *cmd == ' ') { cmd++; len--; }

    // 辅助函数：跳过空格
    auto skip_spaces = [](const char *&p, size_t &l) {
        while (l > 0 && *p == ' ') { p++; l--; }
    };

    // 辅助函数：匹配单词（不区分大小写）
    auto match_word = [](const char *p, size_t l, const char *word) -> bool {
        size_t word_len = strlen(word);
        if (l < word_len) return false;
        return strncasecmp(p, word, word_len) == 0 && (l == word_len || p[word_len] == ' ');
    };

    // 解析 SET 命令
    if (match_word(cmd, len, "SET")) {
        const char *p = cmd + 3;
        size_t l = len - 3;
        skip_spaces(p, l);

        int pin = -2;  // -2 表示未设置

        if (match_word(p, l, "TX")) {
            p += 2; l -= 2;
            skip_spaces(p, l);
            pin = std::atoi(p);

            // 检查是否与RX相同
            {
                std::lock_guard lock(manager.mutex);
                if (pin == manager.rx_pin) {
                    send_data("err: tx cannot be same as rx\n");
                    return;
                }
            }

            if (manager.tx_validator && !manager.tx_validator(pin)) {
                send_data("err\n");
                return;
            }

            {
                std::lock_guard lock(manager.mutex);
                manager.tx_pin = pin;
            }
            manager.reconfigure_pins();
            SPDLOG_INFO("TX pin set to {}", pin);
            send_data("ok\n");
        } else if (match_word(p, l, "RX")) {
            p += 2; l -= 2;
            skip_spaces(p, l);
            pin = std::atoi(p);

            // 检查是否与TX相同
            {
                std::lock_guard lock(manager.mutex);
                if (pin == manager.tx_pin) {
                    send_data("err: rx cannot be same as tx\n");
                    return;
                }
            }

            if (manager.rx_validator && !manager.rx_validator(pin)) {
                send_data("err\n");
                return;
            }

            {
                std::lock_guard lock(manager.mutex);
                manager.rx_pin = pin;
            }
            manager.reconfigure_pins();
            SPDLOG_INFO("RX pin set to {}", pin);
            send_data("ok\n");
        } else if (match_word(p, l, "RTS")) {
            p += 3; l -= 3;
            skip_spaces(p, l);
            pin = std::atoi(p);

            if (manager.rts_validator && !manager.rts_validator(pin)) {
                send_data("err\n");
                return;
            }

            {
                std::lock_guard lock(manager.mutex);
                manager.rts_pin = pin;
            }
            manager.reconfigure_uart();
            SPDLOG_INFO("RTS pin set to {}", pin);
            send_data("ok\n");
        } else if (match_word(p, l, "CTS")) {
            p += 3; l -= 3;
            skip_spaces(p, l);
            pin = std::atoi(p);

            if (manager.cts_validator && !manager.cts_validator(pin)) {
                send_data("err\n");
                return;
            }

            {
                std::lock_guard lock(manager.mutex);
                manager.cts_pin = pin;
            }
            manager.reconfigure_uart();
            SPDLOG_INFO("CTS pin set to {}", pin);
            send_data("ok\n");
        } else if (match_word(p, l, "FLOW")) {
            p += 4; l -= 4;
            skip_spaces(p, l);
            int enable = std::atoi(p);

            {
                std::lock_guard lock(manager.mutex);
                manager.flow_control = (enable != 0);
            }
            manager.reconfigure_uart();
            SPDLOG_INFO("Flow control {}", enable ? "enabled" : "disabled");
            send_data("ok\n");
        } else {
            send_data("err\n");
        }
    } else if (match_word(cmd, len, "GET")) {
        std::lock_guard lock(manager.mutex);
        char resp[80];
        int resp_len = snprintf(resp, sizeof(resp), "tx:%d rx:%d rts:%d cts:%d flow:%d\n",
                                manager.tx_pin, manager.rx_pin, manager.rts_pin,
                                manager.cts_pin, manager.flow_control ? 1 : 0);
        send_data(std::string_view(resp, resp_len));
    } else if (match_word(cmd, len, "HELP")) {
        send_data("commands:\n");
        send_data("  set tx <pin>   - set tx pin\n");
        send_data("  set rx <pin>   - set rx pin\n");
        send_data("  set rts <pin>  - set rts pin (-1 disable)\n");
        send_data("  set cts <pin>  - set cts pin (-1 disable)\n");
        send_data("  set flow <0|1> - enable/disable flow control\n");
        send_data("  get            - get current config\n");
        send_data("  help           - show this help\n");
    } else {
        send_data("err\n");
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
