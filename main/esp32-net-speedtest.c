/**
 * @file speedtest.c
 * @brief Minimal ESP-IDF HTTP download (throughput) test over Wi-Fi STA.
 *
 * This example opens a plain TCP connection (no TLS) to a host serving a file,
 * issues a simple HTTP/1.1 GET request, then measures throughput only for the
 * HTTP body (data after the header terminator CRLFCRLF).
 *
 * Configure the Wi-Fi SSID/PASS and the download endpoint below.
 */

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"

#include "sdkconfig.h"

// Download (HTTP, no TLS here)
#define DL_HOST             "ADD_HOST_IP"       /**< Example: "example.com" or IP "192.168.1.10". */
#define DL_PORT             8080                /**< 8080 for HTTP. For TLS you need esp_tls and different code. */
#define DL_PATH             "/1MB.bin"          /**< Must start with '/', e.g. "/bigfile.bin". */
#define DL_LIMIT_BYTES      0                   /**< 0 = download full response body; otherwise cap bytes (e.g., 5*1024*1024). */

// Upload (TCP)
#define UL_HOST             "ADD_HOST_IP"       /** PC listening on UL_PORT */
#define UL_PORT             5001                /** listening port */
#define UL_TOTAL_BYTES      (1*1024)            /** bytes to send */

/** I/O buffer size used for both download and (future) upload paths. */
#define IO_BUF_SIZE         (32*1024)           /** 32 KB; tune based on RAM and desired throughput */

/** Wi-Fi credentials (edit for your network) */
#define WIFI_SSID           "ADD_AP_SSID"
#define WIFI_PASS           "ADD_AP_PASS"

static const char *TAG = "speedtest";

/* ===== UTILS MACRO =====
 * Make a small port string buffer (e.g., "80") and open a TCP socket.
 * The OPEN_TCP_OR_RETURN macro sets 'sockvar' and on failure returns from the caller.
 */
#define MAKE_PORTSTR(_port, _buf) \
    do { snprintf((_buf), sizeof((_buf)), "%d", (_port)); } while (0)

#define OPEN_TCP_OR_RETURN(_host, _port, _sockvar, _errtag)                      \
    do {                                                                         \
        char __portstr[8];                                                       \
        MAKE_PORTSTR((_port), __portstr);                                        \
        (_sockvar) = connect_tcp((_host), __portstr);                            \
        if ((_sockvar) < 0) {                                                    \
            ESP_LOGE((_errtag), "connect fail: %s:%d", (_host), (_port));        \
            return;                                                              \
        }                                                                        \
    } while (0)

/* ===== PROTOTYPE for helper function ===== */
static int connect_tcp(const char *host, const char *port);

/* ---------- UPLOAD (raw TCP flood) ---------- */
/**
 * @brief Run a raw TCP upload test and report throughput.
 *
 * Steps:
 *  1. Connect to UL_HOST:UL_PORT.
 *  2. Send UL_TOTAL_BYTES of dummy payload in chunks of IO_BUF_SIZE.
 *  3. Shutdown write side to signal EOF, then compute elapsed time and Mbit/s.
 *
 * Expects a TCP server listening on UL_PORT that simply reads and closes.
 */
static void run_upload_test(void) {
    ESP_LOGI(TAG, "Upload: tcp://%s:%d  (send=%u bytes)",
             UL_HOST, UL_PORT, UL_TOTAL_BYTES);

    int s;
    OPEN_TCP_OR_RETURN(UL_HOST, UL_PORT, s, TAG);

    const size_t buf_sz = IO_BUF_SIZE;
    uint8_t *buf = malloc(buf_sz);
    if (!buf) { ESP_LOGE(TAG, "UL: oom"); close(s); return; }
    memset(buf, 0xA5, buf_sz); // dummy payload

    uint64_t start_us = esp_timer_get_time();
    size_t total = 0;

    while (total < UL_TOTAL_BYTES) {
        size_t chunk = MIN(buf_sz, UL_TOTAL_BYTES - total);
        ssize_t w = write(s, buf, chunk);
        if (w < 0) { ESP_LOGE(TAG, "UL: write error"); break; }
        total += (size_t)w;
        // optional: yield
        // vTaskDelay(0);
    }

    /* Signal we're done sending so the server can close cleanly. */
    shutdown(s, SHUT_WR);

    uint64_t end_us = esp_timer_get_time();
    double secs = (end_us - start_us) / 1e6;
    double mbitps = (total * 8.0) / (secs * 1000.0 * 1000.0);

    ESP_LOGI(TAG, "Upload total: %u bytes in %.3f s  => %.2f Mbit/s",
             (unsigned)total, secs, mbitps);

    free(buf);
    close(s);
}

/**
 * @brief Initialize Wi-Fi in STA mode and wait until an IPv4 address is obtained.
 *
 * This function:
 *  - initializes LwIP + default event loop
 *  - creates the default STA netif
 *  - initializes and starts the Wi-Fi driver
 *  - sets power-save to WIFI_PS_NONE (for more stable throughput tests)
 *  - connects to the configured SSID/PASS
 *  - blocks in a small polling loop until an IPv4 address is assigned
 *
 * @note Power save is disabled to reduce latency jitter during speed testing.
 * @warning Blocking loop polls every 500 ms; convert to proper event handling for production code.
 */
static void wifi_init_and_connect(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = (wifi_config_t){ 0 };
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // also OK for WPA3 transition

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Disable power save for max throughput/consistency */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "Connecting to SSID:\"%s\" ...", WIFI_SSID);
    ESP_ERROR_CHECK(esp_wifi_connect());

    /* Wait for IPv4 address */
    esp_netif_ip_info_t ip_info;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip_info.ip));
            break;
        }
    }
}

/**
 * @brief Open a TCP connection to the given host:port.
 *
 * @param host Hostname or IPv4 address string.
 * @param port Port as string (e.g., "80").
 * @return int Connected socket file descriptor on success; -1 on failure.
 *
 * This helper resolves the host (IPv4 only), creates a stream socket, and connects.
 * The returned socket must be closed by the caller.
 */
static int connect_tcp(const char *host, const char *port) {
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET; // IPv4 only (simple)

    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0 || !res) {
        ESP_LOGE(TAG, "getaddrinfo(%s:%s) failed: %d", host, port, err);
        return -1;
    }
    int s = socket(res->ai_family, res->ai_socktype, 0);
    if (s < 0) {
        ESP_LOGE(TAG, "socket failed");
        freeaddrinfo(res);
        return -1;
    }
    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "connect failed");
        close(s);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);
    return s;
}

/**
 * @brief Run a simple HTTP/1.1 download test and report throughput.
 *
 * Steps:
 *  1. Connect via TCP to DL_HOST:DL_PORT.
 *  2. Send a GET request for DL_PATH with "Connection: close".
 *  3. Read response; locate header terminator (CRLFCRLF).
 *  4. Start timing at first byte after headers; accumulate body bytes.
 *  5. Stop on EOF or when DL_LIMIT_BYTES (if > 0) is reached.
 *  6. Log total bytes, elapsed seconds, and computed Mbit/s.
 *
 * @note Timing excludes the HTTP headers; only body bytes are measured.
 * @note For HTTPS/TLS, replace the TCP connect + read/write with esp_tls flow.
 */
static void run_download_test(void) {
    ESP_LOGI(TAG, "Download: http://%s:%d%s  (limit=%u bytes)",
             DL_HOST, DL_PORT, DL_PATH, DL_LIMIT_BYTES);

    int s;
    OPEN_TCP_OR_RETURN(DL_HOST, DL_PORT, s, TAG);

    char req[512];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Connection: close\r\n"
                     "User-Agent: esp32-speedtest\r\n\r\n",
                     DL_PATH, DL_HOST);
    if (write(s, req, n) != n) {
        ESP_LOGE(TAG, "DL: write request fail");
        close(s);
        return;
    }

    const size_t buf_sz = IO_BUF_SIZE;
    uint8_t *buf = malloc(buf_sz);
    if (!buf) { ESP_LOGE(TAG, "DL: OOM"); close(s); return; }

    uint64_t start_us = 0;
    size_t total = 0;
    bool header_done = false;

    for (;;) {
        ssize_t r = read(s, buf, buf_sz);
        if (r < 0) { ESP_LOGE(TAG, "DL: read error"); break; }
        if (r == 0) break; // EOF

        if (!header_done) {
            // search for CRLFCRLF delimiter
            uint8_t *p = (uint8_t*)strstr((char*)buf, "\r\n\r\n");
            if (p) {
                header_done = true;
                start_us = esp_timer_get_time();
                size_t header_len = (p + 4) - buf;
                size_t body_len = r - header_len;
                total += body_len;
            }
            // if not found yet, everything was header -> do not count
        } else {
            total += (size_t)r;
        }

        if (DL_LIMIT_BYTES > 0 && total >= DL_LIMIT_BYTES) break;
    }
    uint64_t end_us = esp_timer_get_time();

    if (header_done) {
        double secs = (end_us - start_us) / 1e6;
        double mbitps = (total * 8.0) / (secs * 1000.0 * 1000.0);

        ESP_LOGI(TAG, "Download BODY: %u bytes in %.3f s  => %.2f Mbit/s",
                 (unsigned)total, secs, mbitps);
    } else {
        ESP_LOGW(TAG, "Header not found; no bytes counted.");
    }

    free(buf);
    close(s);
}

/**
 * @brief ESP-IDF entry point.
 *
 * Initializes NVS, connects to Wi-Fi, runs the download test,
 * then logs completion. Blocks minimally between stages.
 */
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init_and_connect();

    // small stabilization delay
    vTaskDelay(pdMS_TO_TICKS(500));
    run_upload_test();
    printf("\n");
    run_download_test();

    ESP_LOGI(TAG, "Done!");
}
