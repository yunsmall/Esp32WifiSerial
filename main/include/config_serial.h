#pragma once

#include <virtual_device/CdcAcmVirtualInterfaceHandler.h>
#include <Session.h>

namespace usbipdcpp {
    class Session;
}

class WifiSerialManager;

// 配置串口通信接口处理器
class ConfigSerialCommunicationInterfaceHandler : public usbipdcpp::CdcAcmCommunicationInterfaceHandler {
public:
    ConfigSerialCommunicationInterfaceHandler(usbipdcpp::UsbInterface &iface, usbipdcpp::StringPool &sp, WifiSerialManager &mgr);

    void on_set_control_line_state(const usbipdcpp::ControlSignalState &state) override;

    class ConfigSerialDataInterfaceHandler *data_handler = nullptr;
    WifiSerialManager &manager;
};

// 配置串口数据接口处理器
class ConfigSerialDataInterfaceHandler : public usbipdcpp::CdcAcmDataInterfaceHandler {
public:
    ConfigSerialDataInterfaceHandler(usbipdcpp::UsbInterface &iface, usbipdcpp::StringPool &sp, WifiSerialManager &mgr);

    void on_data_received(const usbipdcpp::data_type &data) override;
    void on_new_connection(usbipdcpp::Session &current_session, usbipdcpp::error_code &ec) override;
    void on_disconnection(usbipdcpp::error_code &ec) override;

    std::atomic_bool should_immediately_stop = false;
    WifiSerialManager &manager;

private:
    static constexpr size_t MAX_CMD_LEN = 128;
    char recv_buffer[MAX_CMD_LEN];
    size_t recv_len = 0;

    void process_command(const char *cmd, size_t len);
};
