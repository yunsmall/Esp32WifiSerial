# ESP32 WiFi串口透传

基于USBIP协议的ESP32 WiFi串口透明传输方案。

## 功能特性

- 通过USBIP协议创建两个虚拟串口设备
- **配置串口 (busid 1-1)**：远程配置TX/RX引脚
- **透传串口 (busid 1-2)**：透明转发数据到物理UART
- WiFi连接，支持自动重连
- 运行时引脚重配置

## 环境要求

- ESP-IDF v5.0+
- 主机端USBIP客户端（Windows使用`usbipd-win`，Linux使用`linux-usbip`）

## 配置

通过`idf.py menuconfig`或编辑`sdkconfig`配置：

- WiFi SSID: `CONFIG_WIFI_SERIAL_SSID`
- WiFi密码: `CONFIG_WIFI_SERIAL_PASSWORD`
- 服务器端口: `CONFIG_WIFI_SERIAL_PORT`（默认: 3240）
- 默认TX引脚: `CONFIG_SERIAL_TX_PIN`（默认: 4）
- 默认RX引脚: `CONFIG_SERIAL_RX_PIN`（默认: 5）
- 默认波特率: `CONFIG_SERIAL_BAUD_RATE`（默认: 115200）

## 使用方法

### 1. 编译并烧录

```bash
idf.py build
idf.py flash monitor
```

### 2. 主机端挂载USBIP设备

Windows（使用usbipd-win）：
```powershell
usbipd attach --remote <ESP32_IP> --busid 1-1
usbipd attach --remote <ESP32_IP> --busid 1-2
```

Linux：
```bash
usbip attach --remote <ESP32_IP> --busid 1-1
usbip attach --remote <ESP32_IP> --busid 1-2
```

### 3. 配置串口命令

使用任意终端程序（如PuTTY、minicom）打开配置串口。

命令（不区分大小写）：

| 命令 | 说明 |
|------|------|
| `SET TX <引脚>` | 设置TX引脚 |
| `SET RX <引脚>` | 设置RX引脚 |
| `GET` | 获取当前TX/RX引脚 |
| `HELP` | 显示帮助信息 |

示例：
```
SET TX 4
OK
SET RX 5
OK
GET
TX:4 RX:5
```

### 4. 透传串口

透传串口在主机和物理UART之间透明转发数据。

- 波特率等串口参数由主机自动配置
- 发送到此端口的数据转发到UART TX
- 从UART RX接收的数据发送回主机

## 引脚限制 (ESP32-S3)

- GPIO 26-37：SPI Flash/PSRAM相关（大多数开发板不可用）
- GPIO 0：启动引脚，谨慎使用
- GPIO 45-46：Strapping引脚，需检查开发板配置
- 电源引脚：VDD3P3、VDD3P3_RTC、VDD_SPI、VDD3P3_CPU、VDDA、GND

注意：与ESP32不同，ESP32-S3的GPIO 34-39可以用作输出。

## 架构

```
+------------------+     WiFi/USBIP      +------------------+
|      主机 PC     |<------------------>|      ESP32       |
+------------------+                     +------------------+
| 配置串口         |                     | WifiSerialManager|
| 透传串口         |                     |   - 配置设备     |
+------------------+                     |   - 透传设备     |
                                         +------------------+
                                                    |
                                                    v
                                            物理UART设备
```

## 许可证

GPL-3.0 License
