// Upload (TCP brut; serverul trebuie doar să accepte conexiunea și să citească)
#define UL_HOST         "192.168.1.168"        // PC-ul tău sau un server care dă listen pe UL_PORT
#define UL_PORT         5001                   // portul pe care asculți la upload
#define UL_TOTAL_BYTES  (8*1024*1024)          // cât trimiți: ex. 8 MB
/* ---------- UPLOAD (raw TCP flood) ---------- */
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

    /* Optional: perform a small read to allow TCP ACKs to be processed */
    uint8_t tmp[64];
    read(s, tmp, sizeof(tmp));

    uint64_t end_us = esp_timer_get_time();
    double secs = (end_us - start_us) / 1e6;
    double mbitps = (total * 8.0) / (secs * 1000.0 * 1000.0);

    ESP_LOGI(TAG, "Upload total: %u bytes in %.3f s  => %.2f Mbit/s",
             (unsigned)total, secs, mbitps);

    free(buf);
    close(s);
}