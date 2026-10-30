#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lvgl.h"
#include "lcd.h"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "touch.h"
#include "24cxx.h"
#include "delay.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

CAN_HandleTypeDef hcan1;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;
volatile VehicleData vehicle_data;

static uint8_t esp_rx_byte;
static uint8_t uart1_rx_byte;
static volatile uint8_t esp_rx[512];
static volatile uint16_t esp_wr;
static volatile uint16_t esp_rd;
static char uart1_frame[112];
static volatile uint8_t uart1_frame_len;
static lv_display_t *display;
static lv_obj_t *value_labels[6];
static lv_obj_t *status_label;
static lv_obj_t *credentials_label;
static uint8_t touch_ready;
static uint8_t ui_ready;

static void diag_printf(const char *format, ...)
{
    char message[160];
    va_list args;

    if (huart1.Instance != USART1) return;
    va_start(args, format);
    int length = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (length <= 0) return;
    if ((size_t)length >= sizeof(message)) length = (int)sizeof(message) - 1;
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)message, (uint16_t)length, 250);
}

#if (configAPPLICATION_ALLOCATED_HEAP == 1)
__attribute__((section(".ccmram"), aligned(8)))
uint8_t ucHeap[configTOTAL_HEAP_SIZE];
#endif

static const char *const pid_names[6] = {"LOAD", "TMP", "RPM", "SPD", "MAF", "THR"};
static const char *const pid_units[6] = {"%", "C", "rpm", "km/h", "g/s", "%"};
static const uint8_t pid_codes[6] = {0x04, 0x05, 0x0C, 0x0D, 0x10, 0x11};

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t can_baud; /* 0=automatic, 1=500 kbit/s, 2=250 kbit/s */
    uint8_t backlight;
    char ap_ssid[24];
    char ap_password[16];
    char web_password[20];
    uint16_t checksum;
} DeviceConfig;

7static DeviceConfig config;
static uint8_t active_pid;
static uint8_t can_baud = 1;
static uint8_t can_ready;
static uint32_t last_can_rx;

static void vehicle_snapshot(VehicleData *snapshot)
{
    taskENTER_CRITICAL();
    memcpy(snapshot, (const void *)&vehicle_data, sizeof(*snapshot));
    taskEXIT_CRITICAL();
}
static uint16_t crc16(const uint8_t *p, size_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) { c ^= *p++; for (uint8_t b = 0; b < 8; b++) c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1); }
    return c;
}

static void config_load(void)
{
    memset(&config, 0, sizeof(config));
    config.magic = 0x4F424443UL;
    config.version = 1;
    config.can_baud = 0;
    config.backlight = 100;
    uint32_t id = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
    snprintf(config.ap_ssid, sizeof(config.ap_ssid), "OBD-%08lX", (unsigned long)id);
    snprintf(config.ap_password, sizeof(config.ap_password), "OBD%08lX", (unsigned long)id);
    snprintf(config.web_password, sizeof(config.web_password), "admin%08lX", (unsigned long)id);
    DeviceConfig stored;
    at24cxx_read(64, (uint8_t *)&stored, sizeof(stored));
    if (stored.magic == config.magic && stored.version == config.version &&
        stored.checksum == crc16((const uint8_t *)&stored, offsetof(DeviceConfig, checksum)) &&
        stored.can_baud <= 2 && stored.backlight <= 100 &&
        memchr(stored.ap_ssid, 0, sizeof(stored.ap_ssid)) &&
        memchr(stored.ap_password, 0, sizeof(stored.ap_password)) &&
        memchr(stored.web_password, 0, sizeof(stored.web_password))) {
        config = stored;
    }
}

static void config_save(void)
{
    config.magic = 0x4F424443UL;
    config.version = 1;
    config.checksum = crc16((const uint8_t *)&config, offsetof(DeviceConfig, checksum));
    at24cxx_write(64, (uint8_t *)&config, sizeof(config));
}

static uint8_t can_init(uint8_t baud)
{
    HAL_CAN_Stop(&hcan1);
    hcan1.Init.Prescaler = baud == 2 ? 12 : 6; /* 42 MHz APB1, 14 TQ */
    if (HAL_CAN_Init(&hcan1) != HAL_OK) return 0;
    CAN_FilterTypeDef filter = {0};
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;
    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK || HAL_CAN_Start(&hcan1) != HAL_OK ||
        HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
        (void)HAL_CAN_Stop(&hcan1);
        return 0;
    }
    can_baud = baud;
    return 1;
}

static void obd_send(uint8_t pid)
{
    CAN_TxHeaderTypeDef h = {0};
    uint8_t d[8] = {2, 1, pid, 0, 0, 0, 0, 0};
    uint32_t mailbox;
    h.StdId = 0x7DF; h.IDE = CAN_ID_STD; h.RTR = CAN_RTR_DATA; h.DLC = 8;
    h.TransmitGlobalTime = DISABLE;
    (void)HAL_CAN_AddTxMessage(&hcan1, &h, d, &mailbox);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *can)
{
    CAN_RxHeaderTypeDef h;
    uint8_t d[8];
    if (HAL_CAN_GetRxMessage(can, CAN_RX_FIFO0, &h, d) != HAL_OK || h.DLC < 4 || h.IDE != CAN_ID_STD || h.StdId < 0x7E8 || h.StdId > 0x7EF || d[1] != 0x41) return;
    int16_t v = -1;
    switch (d[2]) {
        case 0x04: v = (int16_t)(d[3] * 100 / 255); break;
        case 0x05: v = (int16_t)d[3] - 40; break;
        case 0x0C: if (h.DLC >= 5) v = (int16_t)((d[3] * 256 + d[4]) / 4); break;
        case 0x0D: v = d[3]; break;
        case 0x10: if (h.DLC >= 5) v = (int16_t)((d[3] * 256 + d[4]) / 100); break;
        case 0x11: v = (int16_t)(d[3] * 100 / 255); break;
        default: return;
    }
    for (unsigned i = 0; i < 6; i++) if (pid_codes[i] == d[2]) {
        vehicle_data.value[i] = v;
        vehicle_data.valid[i] = 1;
        vehicle_data.updated_ms = HAL_GetTick();
        vehicle_data.can_online = 1;
    }
    last_can_rx = HAL_GetTick();
}

static void ui_flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)d;
    lcd_color_fill((uint16_t)a->x1, (uint16_t)a->y1, (uint16_t)a->x2, (uint16_t)a->y2, (uint16_t *)px);
    lv_display_flush_ready(display);
}

static void ui_touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (touch_ready && tp_dev.scan && tp_dev.scan(0)) {
        data->point.x = (lv_coord_t)tp_dev.x[0];
        data->point.y = (lv_coord_t)tp_dev.y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void ui_init(void)
{
    LCD_BL(1);
    lcd_init();
    diag_printf("LCD ID: 0x%04X\r\n", lcddev.id);
    lcd_display_dir(1);
    lv_init();
    lv_tick_set_cb(HAL_GetTick);
    display = lv_display_create(lcddev.width, lcddev.height);
    static lv_color_t draw_buf[480 * 24];
    lv_display_set_buffers(display, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, ui_flush);
    if (lcddev.id == 0x7796 || lcddev.id == 0x5510 || lcddev.id == 0x9806 ||
        lcddev.id == 0x4342 || lcddev.id == 0x4384 || lcddev.id == 0x1018) {
        if (gt9xxx_init() == 0) {
            tp_dev.scan = gt9xxx_scan;
            tp_dev.touchtype = (uint8_t)((lcddev.dir & 1U) | 0x80U);
            touch_ready = 1;
            lv_indev_t *touch = lv_indev_create();
            lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(touch, ui_touch_read);
        }
    } else if (tp_get_adjust_data() && tp_init() == 0) {
        touch_ready = 1;
        lv_indev_t *touch = lv_indev_create();
        lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(touch, ui_touch_read);
    }
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x101820), 0);
    lv_obj_t *title = lv_label_create(lv_screen_active());
    lv_label_set_text(title, "OBD LIVE  |  STM32F407ZG");
    lv_obj_set_style_text_color(title, lv_color_hex(0x35D0BA), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    for (unsigned i = 0; i < 6; i++) {
        lv_obj_t *card = lv_obj_create(lv_screen_active());
        lv_obj_set_size(card, 145, 82);
        lv_obj_set_pos(card, 14 + (i % 3) * 154, 46 + (i / 3) * 94);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1D2B36), 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x355363), 0);
        lv_obj_t *name = lv_label_create(card);
        lv_label_set_text(name, pid_names[i]); lv_obj_set_style_text_color(name, lv_color_hex(0xA9BBC5), 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 2, 0);
        value_labels[i] = lv_label_create(card);
        lv_label_set_text(value_labels[i], "--");
        lv_obj_set_style_text_color(value_labels[i], lv_color_hex(0xF4F7F9), 0);
        lv_obj_align(value_labels[i], LV_ALIGN_CENTER, 0, 4);
        lv_obj_t *unit = lv_label_create(card);
        lv_label_set_text(unit, pid_units[i]); lv_obj_set_style_text_color(unit, lv_color_hex(0x35D0BA), 0);
        lv_obj_align(unit, LV_ALIGN_BOTTOM_RIGHT, -2, 0);
    }
    status_label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xA9BBC5), 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_LEFT, 12, -8);
    credentials_label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_color(credentials_label, lv_color_hex(0x6F8794), 0);
    lv_obj_set_style_text_font(credentials_label, &lv_font_montserrat_14, 0);
    lv_obj_align(credentials_label, LV_ALIGN_BOTTOM_LEFT, 12, -28);
    char credentials[96];
    (void)snprintf(credentials, sizeof(credentials), "AP: %s / %s   WEB: admin / %s",
                   config.ap_ssid, config.ap_password, config.web_password);
    lv_label_set_text(credentials_label, credentials);
    LCD_BL(1);
    ui_ready = 1;
    diag_printf("LVGL display initialized: %ux%u\r\n",
                (unsigned)lcddev.width, (unsigned)lcddev.height);
}

static void esp_write(const char *s)
{
    (void)HAL_UART_Transmit(&huart3, (uint8_t *)s, (uint16_t)strlen(s), 250);
}

static uint8_t esp_command_wait(const char *command, const char *expected, uint32_t timeout_ms)
{
    char response[160];
    size_t used = 0;
    uint32_t started = HAL_GetTick();

    taskENTER_CRITICAL();
    esp_rd = esp_wr;
    taskEXIT_CRITICAL();
    esp_write(command);

    while (HAL_GetTick() - started < timeout_ms) {
        while (esp_rd != esp_wr) {
            char ch = (char)esp_rx[esp_rd];
            esp_rd = (uint16_t)((esp_rd + 1U) % sizeof(esp_rx));
            if (used + 1U < sizeof(response)) {
                response[used++] = ch;
                response[used] = '\0';
                if (strstr(response, expected) != NULL) return 1;
            } else {
                used = 0;
                response[0] = '\0';
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    return 0;
}

static uint8_t esp_wait_prompt(uint32_t timeout_ms)
{
    uint32_t started = HAL_GetTick();
    while (HAL_GetTick() - started < timeout_ms) {
        while (esp_rd != esp_wr) {
            uint8_t ch = esp_rx[esp_rd];
            esp_rd = (uint16_t)((esp_rd + 1U) % sizeof(esp_rx));
            if (ch == '>') return 1;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return 0;
}

static void esp_send_response(uint8_t id, const char *header, const char *body)
{
    char cmd[40];
    size_t header_len = strlen(header), body_len = strlen(body);
    int n = snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u,%u\r\n", id, (unsigned)(header_len + body_len));
    (void)HAL_UART_Transmit(&huart3, (uint8_t *)cmd, (uint16_t)n, 100);
    if (esp_wait_prompt(500)) {
        (void)HAL_UART_Transmit(&huart3, (uint8_t *)header, (uint16_t)header_len, 250);
        if (body_len) (void)HAL_UART_Transmit(&huart3, (uint8_t *)body, (uint16_t)body_len, 500);
    }
    n = snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%u\r\n", id);
    (void)HAL_UART_Transmit(&huart3, (uint8_t *)cmd, (uint16_t)n, 100);
}

static void esp_reply(uint8_t id, const char *body, const char *type)
{
    char header[192];
    (void)snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nConnection: close\r\nContent-Length: %u\r\nCache-Control: no-store\r\n\r\n",
                   type, (unsigned)strlen(body));
    esp_send_response(id, header, body);
}

static void base64_encode(const char *src, char *dst, size_t capacity)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out = 0;
    for (size_t i = 0; src[i] && out + 4 < capacity; i += 3) {
        uint32_t v = (uint8_t)src[i] << 16;
        uint8_t remaining = (uint8_t)strlen(src + i);
        if (remaining > 1) v |= (uint8_t)src[i + 1] << 8;
        if (remaining > 2) v |= (uint8_t)src[i + 2];
        dst[out++] = table[(v >> 18) & 63]; dst[out++] = table[(v >> 12) & 63];
        dst[out++] = remaining > 1 ? table[(v >> 6) & 63] : '=';
        dst[out++] = remaining > 2 ? table[v & 63] : '=';
    }
    dst[out] = 0;
}

static uint8_t request_authenticated(const char *req)
{
    char credentials[40], expected[64], encoded[88];
    (void)snprintf(credentials, sizeof(credentials), "admin:%s", config.web_password);
    base64_encode(credentials, expected, sizeof(expected));
    const char *header = strstr(req, "Authorization: Basic ");
    if (!header) header = strstr(req, "authorization: Basic ");
    if (!header) return 0;
    header += strlen("Authorization: Basic ");
    size_t n = 0;
    while (header[n] && header[n] != '\r' && header[n] != '\n' && n + 1 < sizeof(encoded)) { encoded[n] = header[n]; n++; }
    encoded[n] = 0;
    return strcmp(encoded, expected) == 0;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static uint8_t query_value(const char *query, const char *key, char *out, size_t cap)
{
    size_t key_len = strlen(key);
    while (query && *query) {
        const char *end = strchr(query, '&');
        size_t part_len = end ? (size_t)(end - query) : strlen(query);
        if (part_len > key_len && memcmp(query, key, key_len) == 0 && query[key_len] == '=') {
            const char *src = query + key_len + 1;
            size_t src_len = part_len - key_len - 1, n = 0;
            while (src_len && n + 1 < cap) {
                if (*src == '+' ) { out[n++] = ' '; src++; src_len--; }
                else if (*src == '%' && src_len >= 3) {
                    int hi = hex_digit(src[1]), lo = hex_digit(src[2]);
                    if (hi < 0 || lo < 0) return 0;
                    out[n++] = (char)((hi << 4) | lo); src += 3; src_len -= 3;
                } else { out[n++] = *src++; src_len--; }
            }
            if (src_len != 0) return 0;
            out[n] = 0;
            return 1;
        }
        if (!end) break;
        query = end + 1;
    }
    return 0;
}

static uint8_t config_text_valid(const char *text, size_t min_len)
{
    size_t n = strlen(text);
    if (n < min_len) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x20 || c > 0x7E || c == '"' || c == '\\') return 0;
    }
    return 1;
}

static void esp_http(uint8_t id, char *req)
{
    if (!request_authenticated(req)) {
        const char *unauth = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"OBD\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        esp_send_response(id, unauth, "");
        return;
    }
    if (strstr(req, "GET /api/data")) {
        char body[384];
        VehicleData snapshot;
        vehicle_snapshot(&snapshot);
        int n = snprintf(body, sizeof(body), "{\"load\":%d,\"tmp\":%d,\"rpm\":%d,\"spd\":%d,\"maf\":%d,\"thr\":%d,\"valid\":[%u,%u,%u,%u,%u,%u],\"can\":%s,\"wifi\":%s}",
            snapshot.value[0], snapshot.value[1], snapshot.value[2], snapshot.value[3], snapshot.value[4], snapshot.value[5],
            snapshot.valid[0], snapshot.valid[1], snapshot.valid[2], snapshot.valid[3], snapshot.valid[4], snapshot.valid[5],
            snapshot.can_online ? "true" : "false", snapshot.wifi_online ? "true" : "false");
        (void)n; esp_reply(id, body, "application/json");
    } else if (strstr(req, "GET /api/config?")) {
        const char *q = strchr(req, '?'); char value[24];
        if (q) {
            DeviceConfig candidate = config;
            uint8_t valid = 1;
            q++;
            if (query_value(q, "ssid", value, sizeof(value)) && value[0]) { if (config_text_valid(value, 1)) strcpy(candidate.ap_ssid, value); else valid = 0; }
            if (query_value(q, "ap", value, sizeof(value)) && value[0]) { if (strlen(value) < sizeof(candidate.ap_password) && config_text_valid(value, 8)) strcpy(candidate.ap_password, value); else valid = 0; }
            if (query_value(q, "web", value, sizeof(value)) && value[0]) { if (strlen(value) < sizeof(candidate.web_password) && config_text_valid(value, 8)) strcpy(candidate.web_password, value); else valid = 0; }
            if (query_value(q, "baud", value, sizeof(value))) { if (value[0] >= '0' && value[0] <= '2' && value[1] == 0) candidate.can_baud = (uint8_t)(value[0] - '0'); else valid = 0; }
            if (query_value(q, "bl", value, sizeof(value))) { char *end = NULL; long bl = strtol(value, &end, 10); if (end && *end == 0 && bl >= 0 && bl <= 100) candidate.backlight = (uint8_t)bl; else valid = 0; }
            if (valid && config_text_valid(candidate.ap_ssid, 1) && config_text_valid(candidate.ap_password, 8) && config_text_valid(candidate.web_password, 8)) {
                config = candidate;
                config_save();
                LCD_BL(config.backlight != 0);
                static const char saved[] = "{\"saved\":true,\"reboot_wifi\":true}";
                esp_reply(id, saved, "application/json");
                esp_write("AT+CIPSERVER=0\r\n"); vTaskDelay(pdMS_TO_TICKS(100));
                char cmd[96]; (void)snprintf(cmd, sizeof(cmd), "AT+CWSAP=\"%s\",\"%s\",6,3\r\n", config.ap_ssid, config.ap_password);
                esp_write(cmd); vTaskDelay(pdMS_TO_TICKS(300)); esp_write("AT+CIPSERVER=1,80\r\n");
                return;
            }
        }
        const char error[] = "{\"saved\":false}"; esp_reply(id, error, "application/json");
    } else {
        static const char clean_page[] = "<!doctype html><meta name=viewport content='width=device-width'><title>OBD Live</title><style>body{font:16px system-ui;max-width:700px;margin:24px auto;padding:0 14px;background:#111a20;color:#eef}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.card{background:#1d2b36;padding:12px;border:1px solid #355363}input,select,button{box-sizing:border-box;width:100%;padding:10px;margin:5px 0;background:#24343d;color:#fff;border:1px solid #567}b{color:#42d6ba}</style><h2>OBD Live</h2><div class=grid id=d></div><h3>Device Configuration</h3><form id=f><input name=ssid placeholder='Wi-Fi name (blank keeps current)' maxlength=23><input name=ap type=password placeholder='Wi-Fi password (blank keeps current)' maxlength=15><input name=web type=password placeholder='Admin password (blank keeps current)' maxlength=19><select name=baud><option value=0>CAN Auto</option><option value=1>CAN 500 kbit/s</option><option value=2>CAN 250 kbit/s</option></select><input name=bl type=number min=0 max=100 value=100><button>Save and restart Wi-Fi</button></form><p id=s></p><script>const names=['LOAD','TMP','RPM','SPD','MAF','THR'];async function poll(){try{let x=await(await fetch('/api/data')).json(),v=[x.load,x.tmp,x.rpm,x.spd,x.maf,x.thr];d.innerHTML=names.map((n,i)=>'<div class=card><b>'+n+'</b><div>'+v[i]+(x.valid[i]?'':' (stale)')+'</div></div>').join('')}catch(e){}}setInterval(poll,1000);poll();f.onsubmit=async e=>{e.preventDefault();let x=await(await fetch('/api/config?'+new URLSearchParams(new FormData(f)))).json();s.textContent=x.saved?'Saved. Wi-Fi is restarting.':'Invalid configuration.'};</script>";
        esp_reply(id, clean_page, "text/html; charset=utf-8");
    }
}

static void can_task(void *arg)
{
    (void)arg;
    uint8_t scan = 1;
    for (;;) {
        if (!can_ready) {
            uint8_t baud = config.can_baud == 0 ? scan : config.can_baud;
            can_ready = can_init(baud);
            if (can_ready && config.can_baud == 0) {
                obd_send(0x0C);
                vTaskDelay(pdMS_TO_TICKS(120));
                if (!vehicle_data.can_online) {
                    can_ready = 0;
                    scan = scan == 1 ? 2 : 1;
                }
            } else if (!can_ready) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        } else if (config.can_baud == 0 && !vehicle_data.can_online) {
            can_ready = 0;
        } else {
            obd_send(pid_codes[active_pid]); active_pid = (uint8_t)((active_pid + 1U) % 6U);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (HAL_GetTick() - last_can_rx > 3000) vehicle_data.can_online = 0;
    }
}

static void ui_task(void *arg)
{
    (void)arg;
    if (!ui_ready) {
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        char text[48];
        VehicleData snapshot;
        vehicle_snapshot(&snapshot);
        for (unsigned i = 0; i < 6; i++) {
            int16_t v = snapshot.value[i];
            if (snapshot.valid[i] && snapshot.can_online) (void)snprintf(text, sizeof(text), "%d", v);
            else (void)strcpy(text, "--");
            lv_label_set_text(value_labels[i], text);
        }
        (void)snprintf(text, sizeof(text), "CAN: %s   Wi-Fi: %s 192.168.4.1", snapshot.can_online ? "ONLINE" : "WAITING", snapshot.wifi_online ? "AP" : "STARTING");
        lv_label_set_text(status_label, text);
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void wifi_task(void *arg)
{
    (void)arg;
    uint8_t ready = 0;
    if (!esp_command_wait("AT\r\n", "OK", 1500)) {
        diag_printf("ESP AT timeout: check EN/RST/UART/power\r\n");
    } else {
        diag_printf("ESP AT response OK\r\n");
        ready = esp_command_wait("ATE0\r\n", "OK", 1000) &&
                esp_command_wait("AT+CWMODE=2\r\n", "OK", 1500);
    }
    char cmd[96];
    if (ready) {
        (void)snprintf(cmd, sizeof(cmd), "AT+CWSAP=\"%s\",\"%s\",6,3\r\n", config.ap_ssid, config.ap_password);
        ready = esp_command_wait(cmd, "OK", 2500) &&
                esp_command_wait("AT+CIPMUX=1\r\n", "OK", 1500) &&
                esp_command_wait("AT+CIPSERVER=1,80\r\n", "OK", 1500);
    }
    if (ready) {
        vehicle_data.wifi_online = 1;
        diag_printf("ESP AP and HTTP server ready\r\n");
    } else {
        vehicle_data.wifi_online = 0;
        diag_printf("ESP AP setup failed\r\n");
    }
    char request[384]; uint16_t used = 0;
    for (;;) {
        while (esp_rd != esp_wr) {
            uint8_t ch = esp_rx[esp_rd]; esp_rd = (uint16_t)((esp_rd + 1U) % sizeof(esp_rx));
            if (used + 1U < sizeof(request)) request[used++] = (char)ch; else used = 0;
            if (used >= 4 && memcmp(&request[used - 4], "\r\n\r\n", 4) == 0) {
                request[used] = 0;
                char *ipd = strstr(request, "+IPD,");
                if (ipd) {
                    char *id_end = NULL;
                    unsigned long id = strtoul(ipd + 5, &id_end, 10);
                    char *colon = id_end ? strchr(id_end, ':') : NULL;
                    if (id_end != ipd + 5 && colon && id <= 4) esp_http((uint8_t)id, colon + 1);
                }
                used = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance == USART3) {
        uint16_t next = (uint16_t)((esp_wr + 1U) % sizeof(esp_rx));
        if (next != esp_rd) { esp_rx[esp_wr] = esp_rx_byte; esp_wr = next; }
        (void)HAL_UART_Receive_IT(&huart3, &esp_rx_byte, 1);
    } else if (uart->Instance == USART1) {
        if (uart1_rx_byte == '[') uart1_frame_len = 0;
        if (uart1_frame_len < sizeof(uart1_frame) - 1) uart1_frame[uart1_frame_len++] = (char)uart1_rx_byte;
        else uart1_frame_len = 0;
        if (uart1_rx_byte == ']' && uart1_frame_len > 2) {
            uart1_frame[uart1_frame_len] = 0;
            int values[6];
            if (sscanf((const char *)uart1_frame, "[LOAD=%d TMP=%d RPM=%d SPD=%d MAF=%d THR=%d]", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) == 6) {
                for (unsigned i = 0; i < 6; i++) { vehicle_data.value[i] = (int16_t)values[i]; vehicle_data.valid[i] = 1; }
                vehicle_data.updated_ms = HAL_GetTick();
            }
            uart1_frame_len = 0;
        }
        (void)HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);
    }
}

static void clock_init(void)
{
    RCC_OscInitTypeDef osc = {0}; RCC_ClkInitTypeDef clk = {0};
    __HAL_RCC_PWR_CLK_ENABLE(); __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE; osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON; osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8; osc.PLL.PLLN = 336; osc.PLL.PLLP = RCC_PLLP_DIV2; osc.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK; clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4; clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) Error_Handler();
}

static void gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = LCD_BL_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LCD_BL_GPIO_PORT, &gpio);
    HAL_GPIO_WritePin(LCD_BL_GPIO_PORT, LCD_BL_GPIO_PIN, GPIO_PIN_SET);
}

static void esp_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = ESP_EN_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ESP_EN_GPIO_PORT, &gpio);

    gpio.Pin = ESP_RST_GPIO_PIN;
    HAL_GPIO_Init(ESP_RST_GPIO_PORT, &gpio);

    HAL_GPIO_WritePin(ESP_EN_GPIO_PORT, ESP_EN_GPIO_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ESP_RST_GPIO_PORT, ESP_RST_GPIO_PIN, GPIO_PIN_SET);
    diag_printf("ESP EN asserted high\r\n");
    diag_printf("ESP reset pulse start\r\n");

    HAL_GPIO_WritePin(ESP_RST_GPIO_PORT, ESP_RST_GPIO_PIN, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(ESP_RST_GPIO_PORT, ESP_RST_GPIO_PIN, GPIO_PIN_SET);
    HAL_Delay(500);
    diag_printf("ESP reset complete\r\n");
}

static void uart_init(UART_HandleTypeDef *u, USART_TypeDef *instance, uint32_t baud)
{
    u->Instance = instance; u->Init.BaudRate = baud; u->Init.WordLength = UART_WORDLENGTH_8B;
    u->Init.StopBits = UART_STOPBITS_1; u->Init.Parity = UART_PARITY_NONE;
    u->Init.Mode = UART_MODE_TX_RX; u->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    u->Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(u) != HAL_OK) Error_Handler();
}

int main(void)
{
    /* A bootloader or debugger reset may leave PRIMASK set. */
    __enable_irq();
    HAL_Init();
    clock_init();
    gpio_init();
    delay_init(168);

    uart_init(&huart1, USART1, 460800);
    esp_gpio_init();
    at24cxx_init();
    config_load();
    uart_init(&huart3, USART3, 115200);
    hcan1.Instance = CAN1; hcan1.Init.Mode = CAN_MODE_NORMAL; hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
    hcan1.Init.TimeSeg1 = CAN_BS1_11TQ; hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
    hcan1.Init.TimeTriggeredMode = DISABLE; hcan1.Init.AutoBusOff = ENABLE; hcan1.Init.AutoWakeUp = DISABLE;
    hcan1.Init.AutoRetransmission = ENABLE; hcan1.Init.ReceiveFifoLocked = DISABLE; hcan1.Init.TransmitFifoPriority = DISABLE;
    (void)HAL_UART_Receive_IT(&huart3, &esp_rx_byte, 1);
    (void)HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);
    ui_init();

    memset((void *)&vehicle_data, 0, sizeof(vehicle_data));
    for (unsigned i = 0; i < 6; i++) vehicle_data.value[i] = -1;
    can_ready = can_init(config.can_baud == 2 ? 2 : 1);
    (void)xTaskCreate(can_task, "can", 512, NULL, 3, NULL);
    (void)xTaskCreate(ui_task, "ui", 1024, NULL, 2, NULL);
    (void)xTaskCreate(wifi_task, "wifi", 768, NULL, 2, NULL);
    vTaskStartScheduler();
    Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq(); for (;;) { }
}

void vApplicationMallocFailedHook(void)
{
    Error_Handler();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    Error_Handler();
}
