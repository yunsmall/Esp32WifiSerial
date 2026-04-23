# ESP32 WiFi串口透传

[English](README.md)

基于USBIP协议的ESP32-S3 WiFi串口透明传输方案。通过WiFi创建虚拟串口设备，主机端如同使用本地USB串口设备。

## 功能特性

- **双虚拟串口**：通过USBIP协议创建
  - 配置串口 (busid 1-1)：远程配置接口
  - 透传串口 (busid 1-2)：数据透明转发到物理UART
- **硬件流控**：支持RTS/CTS流控
- **运行时配置**：无需重新烧录即可修改引脚
- **自动重连**：WiFi断线自动恢复
- **低内存占用**：针对嵌入式系统优化

## 环境要求

- ESP-IDF v5.0+
- ESP32-S3 开发板
- 主机端USBIP客户端：
  - Windows: [usbipd-win](https://github.com/dorssel/usbipd-win)
  - Linux: `linux-usbip` 软件包

## 配置

通过 `idf.py menuconfig` 或编辑 `sdkconfig.defaults` 配置：

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `CONFIG_WIFI_SERIAL_SSID` | WiFi名称 | - |
| `CONFIG_WIFI_SERIAL_PASSWORD` | WiFi密码 | - |
| `CONFIG_WIFI_SERIAL_PORT` | USBIP服务器端口 | 3240 |
| `CONFIG_SERIAL_TX_PIN` | 默认TX引脚 | 4 |
| `CONFIG_SERIAL_RX_PIN` | 默认RX引脚 | 5 |
| `CONFIG_SERIAL_BAUD_RATE` | 默认波特率 | 115200 |

## 使用方法

### 1. 编译并烧录

```bash
idf.py build
idf.py -p <端口> flash monitor
```

### 2. 挂载USBIP设备

**Windows（管理员PowerShell）：**
```powershell
usbipd attach --remote <ESP32_IP> --busid 1-1
usbipd attach --remote <ESP32_IP> --busid 1-2
```

**Linux：**
```bash
sudo modprobe vhci-hcd
usbip attach --remote <ESP32_IP> --busid 1-1
usbip attach --remote <ESP32_IP> --busid 1-2
```

### 3. 配置串口命令

使用任意终端程序打开配置串口。

| 命令 | 说明 |
|------|------|
| `set tx <引脚>` | 设置TX引脚 |
| `set rx <引脚>` | 设置RX引脚 |
| `set rts <引脚>` | 设置RTS引脚（-1禁用） |
| `set cts <引脚>` | 设置CTS引脚（-1禁用） |
| `set flow <0\|1>` | 启用/禁用硬件流控 |
| `get` | 获取当前配置 |
| `help` | 显示帮助信息 |

示例：
```
set tx 4
ok
set rx 5
ok
set rts 6
ok
set cts 7
ok
set flow 1
ok
get
tx:4 rx:5 rts:6 cts:7 flow:1
```

### 4. 透传串口

透传串口在主机和物理UART之间双向转发数据：

- 主机 → 透传串口 → UART TX
- UART RX → 透传串口 → 主机
- 波特率、数据位、校验位、停止位由主机软件自动配置
- 启用流控后支持RTS/CTS硬件流控

## 引脚限制 (ESP32-S3)

| GPIO | 说明 |
|------|------|
| 0 | 启动引脚，谨慎使用 |
| 26-37 | SPI Flash/PSRAM（通常不可用） |
| 45-46 | Strapping引脚 |
| 34-39 | 可用作输出（与ESP32不同） |

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