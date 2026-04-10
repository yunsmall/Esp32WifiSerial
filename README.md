# WiFi Serial for ESP32

USBIP-based WiFi serial port transparent transmission for ESP32.

## Features

- Creates two virtual serial ports via USBIP protocol
- **Config Serial (busid 1-1)**: Configure TX/RX pins remotely
- **Transparent Serial (busid 1-2)**: Transparent data forwarding to physical UART
- WiFi connectivity with automatic reconnection
- Runtime pin reconfiguration

## Requirements

- ESP-IDF v5.0+
- USBIP client on host machine (e.g., `usbipd-win` on Windows, `linux-usbip` on Linux)

## Configuration

Configure via `idf.py menuconfig` or edit `sdkconfig`:

- WiFi SSID: `CONFIG_WIFI_SERIAL_SSID`
- WiFi Password: `CONFIG_WIFI_SERIAL_PASSWORD`
- Server Port: `CONFIG_WIFI_SERIAL_PORT` (default: 3240)
- Default TX Pin: `CONFIG_SERIAL_TX_PIN` (default: 4)
- Default RX Pin: `CONFIG_SERIAL_RX_PIN` (default: 5)
- Default Baud Rate: `CONFIG_SERIAL_BAUD_RATE` (default: 115200)

## Usage

### 1. Build and Flash

```bash
idf.py build
idf.py flash monitor
```

### 2. Attach USBIP Device on Host

On Windows (using usbipd-win):
```powershell
usbipd attach --remote <ESP32_IP> --busid 1-1
usbipd attach --remote <ESP32_IP> --busid 1-2
```

On Linux:
```bash
usbip attach --remote <ESP32_IP> --busid 1-1
usbip attach --remote <ESP32_IP> --busid 1-2
```

### 3. Config Serial Commands

Open the config serial port with any terminal program (e.g., PuTTY, minicom).

Commands (case-insensitive):

| Command | Description |
|---------|-------------|
| `SET TX <pin>` | Set TX pin |
| `SET RX <pin>` | Set RX pin |
| `GET` | Get current TX/RX pins |
| `HELP` | Show help message |

Example:
```
SET TX 4
OK
SET RX 5
OK
GET
TX:4 RX:5
```

### 4. Transparent Serial

The transparent serial port forwards data between host and physical UART.

- Baud rate and other serial parameters are automatically configured by host
- Data sent to this port is forwarded to UART TX
- Data received from UART RX is sent back to host

## Pin Restrictions (ESP32-S3)

- GPIO 26-37: SPI Flash/PSRAM related (not available on most boards)
- GPIO 0: Boot pin, use with caution
- GPIO 45-46: Strapping pins, check board configuration
- Power pins: VDD3P3, VDD3P3_RTC, VDD_SPI, VDD3P3_CPU, VDDA, GND

Note: Unlike ESP32, ESP32-S3 GPIO 34-39 can be used as output.

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
