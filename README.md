# WiFi Serial for ESP32

[中文文档](README-zh.md)

USBIP-based WiFi serial port transparent transmission for ESP32-S3. Creates virtual serial ports over WiFi that behave like local USB serial devices.

## Features

- **Two Virtual Serial Ports** via USBIP protocol
  - Config Serial (busid 1-1): Remote configuration interface
  - Transparent Serial (busid 1-2): Data forwarding to physical UART
- **Hardware Flow Control**: RTS/CTS support
- **Runtime Configuration**: Change pins without reflashing
- **Auto Reconnection**: WiFi disconnect recovery
- **Low Memory Footprint**: Optimized for embedded systems

## Requirements

- ESP-IDF v5.0+
- ESP32-S3 development board
- USBIP client on host:
  - Windows: [usbipd-win](https://github.com/dorssel/usbipd-win)
  - Linux: `linux-usbip` package

## Configuration

Configure via `idf.py menuconfig` or edit `sdkconfig.defaults`:

| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_WIFI_SERIAL_SSID` | WiFi SSID | - |
| `CONFIG_WIFI_SERIAL_PASSWORD` | WiFi Password | - |
| `CONFIG_WIFI_SERIAL_PORT` | USBIP Server Port | 3240 |
| `CONFIG_SERIAL_TX_PIN` | Default TX Pin | 4 |
| `CONFIG_SERIAL_RX_PIN` | Default RX Pin | 5 |
| `CONFIG_SERIAL_BAUD_RATE` | Default Baud Rate | 115200 |

## Usage

### 1. Build and Flash

```bash
idf.py build
idf.py -p <PORT> flash monitor
```

### 2. Connect USBIP Devices

**Windows (PowerShell as Administrator):**
```powershell
usbipd attach --remote <ESP32_IP> --busid 1-1
usbipd attach --remote <ESP32_IP> --busid 1-2
```

**Linux:**
```bash
sudo modprobe vhci-hcd
usbip attach --remote <ESP32_IP> --busid 1-1
usbip attach --remote <ESP32_IP> --busid 1-2
```

### 3. Config Serial Commands

Open the config serial port with any terminal program.

| Command | Description |
|---------|-------------|
| `set tx <pin>` | Set TX pin |
| `set rx <pin>` | Set RX pin |
| `set rts <pin>` | Set RTS pin (-1 to disable) |
| `set cts <pin>` | Set CTS pin (-1 to disable) |
| `set flow <0\|1>` | Enable/disable hardware flow control |
| `get` | Get current configuration |
| `help` | Show help message |

Example:
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

### 4. Transparent Serial

The transparent serial port forwards data bidirectionally between host and physical UART:

- Host → Transparent Serial → UART TX
- UART RX → Transparent Serial → Host
- Baud rate, data bits, parity, stop bits are configured by host software
- Hardware flow control (RTS/CTS) when enabled

## Pin Restrictions (ESP32-S3)

| GPIO | Notes |
|------|-------|
| 0 | Boot pin, use with caution |
| 26-37 | SPI Flash/PSRAM (usually unavailable) |
| 45-46 | Strapping pins |
| 34-39 | Can be used as output (unlike ESP32) |

## Architecture

```
+------------------+     WiFi/USBIP      +------------------+
|      Host PC     |<------------------>|      ESP32       |
+------------------+                     +------------------+
| Config Serial    |                     | WifiSerialManager|
| Transparent Ser. |                     |   - Config Dev   |
+------------------+                     |   - Transparent  |
                                         +------------------+
                                                    |
                                                    v
                                         Physical UART Device
```

## License

GPL-3.0 License