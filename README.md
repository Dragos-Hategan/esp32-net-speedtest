# ESP-IDF Wi-Fi HTTP Speedtest (Download)

Minimal **throughput test** over Wi-Fi STA using a plain HTTP/1.1 GET (no TLS).  
It measures **only the HTTP body** (everything after `\r\n\r\n`), ignoring the headers.

---

## What it does

1. Initializes Wi-Fi in **STA mode** and connects to your AP.  
   Power-save is disabled (`WIFI_PS_NONE`) for stable throughput.
2. Opens a TCP connection to `DL_HOST:DL_PORT`.
3. Sends a `GET DL_PATH HTTP/1.1` with `Connection: close`.
4. Finds the header terminator `\r\n\r\n` and starts the timer **on the first body byte**.
5. Reads until EOF or until `DL_LIMIT_BYTES` is reached.
6. Logs: bytes received, elapsed time, and throughput (Mbit/s).

---

## Configuration

At the top of `speedtest.c`, edit:

```c
#define DL_HOST   "ADD_HOST_IP"
#define WIFI_SSID "ADD_AP_SSID"
#define WIFI_PASS "ADD_AP_PASS"
```
---

## Requirements

- ESP-IDF (v5.x recommended)
- Python 3 (for serving test file and upload sink)
- A Wi-Fi AP your ESP32 can join

---

## 1. Creating the Test File (1MB.bin)
- In Windows, create a new folder with the file of 1 MB size (it is already inside /download folder)
- Windows (PowerShell):
```Windows (PowerShell)
fsutil file createnew 1MB.bin 1048576
```

---

## 2. Start the servers
- Download (HTTP) server — give this command inside the download folder, containing 1MB.bin:
```cmd
python -m http.server 8080
```
- Upload (TCP) sink — in a separate terminal, from `upload/`:
```cmd
cd upload
python tcp_sink.py
```

---

## 3. After starting both servers, open monitor for ESP32
![Console output](/docs/Console.png)

---
