#pragma once

#include <virtual_device/CdcAcmVirtualInterfaceHandler.h>
#include <atomic>
#include <thread>

namespace usbipdcpp {
    class Session;
}

class WifiSerialManager;

// 透传串口通信接口处理器
class TransparentSerialCommunicationInterfaceHandler : public usbipdcpp::CdcAcmCommunicationInterfaceHandler {
public:
    TransparentSerialCommunicationInterfaceHandler(usbipdcpp::UsbInterface &iface, usbipdcpp::StringPool &sp, WifiSerialManager &mgr);

    void on_set_line_coding(const usbipdcpp::LineCoding &coding) override;
    void on_set_control_line_state(const usbipdcpp::ControlSignalState &state) override;

    WifiSerialManager &manager;
};

// 透传串口数据接口处理器
class TransparentSerialDataInterfaceHandler : public usbipdcpp::CdcAcmDataInterfaceHandler {
public:
    TransparentSerialDataInterfaceHandler(usbipdcpp::UsbInterface &iface, usbipdcpp::StringPool &sp, WifiSerialManager &mgr);

    void on_new_connection(usbipdcpp::Session &current_session, usbipdcpp::error_code &ec) override;
    void on_disconnection(usbipdcpp::error_code &ec) override;
    void on_data_received(usbipdcpp::data_type &&data) override;
    void on_rts_changed(bool rts) override;

    std::atomic_bool should_immediately_stop = false;
    std::atomic_bool host_ready_to_receive = false;
    WifiSerialManager &manager;
    std::thread uart_receive_thread;
};
