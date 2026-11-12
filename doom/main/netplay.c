#include "netplay.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "nvs_flash.h"

static const char *TAG = "netplay";

// Game access points are recognised by this prefix and share a fixed
// password, which keeps phones and laptops from wandering in.
#define SSID_PREFIX "DOOM "
#define PASSWORD "cyberdoom"
#define CHANNEL 6
#define MAX_CLIENTS 3 // four players including the host

#define NAMES_FILE "/data/netplay.txt"

char netplay_player_name[NETPLAY_NAME_LEN + 1];
char netplay_game_name[NETPLAY_NAME_LEN + 1];

#define MSG_MAGIC 0x4D53474Eu
static RTC_NOINIT_ATTR uint32_t s_msg_magic;
static RTC_NOINIT_ATTR char s_msg[96];

static EventGroupHandle_t s_events;
#define EV_GOT_IP BIT0
#define EV_FAILED BIT1

static char s_server[16];
static wifi_mode_t s_mode = WIFI_MODE_NULL;

char netplay_current_game[NETPLAY_NAME_LEN + 1];

// ---- names --------------------------------------------------------------------

static void read_line(FILE *f, char *out)
{
    char line[64];
    if (!fgets(line, sizeof(line), f))
        return;
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0])
        snprintf(out, NETPLAY_NAME_LEN + 1, "%.*s", NETPLAY_NAME_LEN, line);
}

void netplay_load_names(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(netplay_player_name, sizeof(netplay_player_name), "PI-%02X%02X", mac[4], mac[5]);
    snprintf(netplay_game_name, sizeof(netplay_game_name), "%s", netplay_player_name);

    FILE *f = fopen(NAMES_FILE, "r");
    if (f) {
        read_line(f, netplay_player_name);
        read_line(f, netplay_game_name);
        fclose(f);
    }
}

void netplay_save_names(void)
{
    FILE *f = fopen(NAMES_FILE, "w");
    if (f) {
        fprintf(f, "%s\n%s\n", netplay_player_name, netplay_game_name);
        fclose(f);
    }
}

// ---- messages across restarts -------------------------------------------------

void netplay_set_message(const char *msg)
{
    snprintf(s_msg, sizeof(s_msg), "%s", msg);
    s_msg_magic = MSG_MAGIC;
}

const char *netplay_take_message(void)
{
    if (s_msg_magic != MSG_MAGIC)
        return NULL;
    s_msg_magic = 0;
    s_msg[sizeof(s_msg) - 1] = '\0';
    return s_msg;
}

// ---- Wi-Fi --------------------------------------------------------------------

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *e = data;
        ESP_LOGI(TAG, "player joined the access point (" MACSTR ")", MAC2STR(e->mac));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *e = data;
        ESP_LOGI(TAG, "player left the access point (" MACSTR ")", MAC2STR(e->mac));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_events, EV_FAILED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        // The host's access point is the gateway, and its server listens there.
        esp_ip4addr_ntoa(&e->ip_info.gw, s_server, sizeof(s_server));
        ESP_LOGI(TAG, "got " IPSTR ", server at %s", IP2STR(&e->ip_info.ip), s_server);
        xEventGroupSetBits(s_events, EV_GOT_IP);
    }
}

static void wifi_init(void)
{
    ESP_LOGI(TAG, "starting Wi-Fi: %u bytes internal RAM free, largest block %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    esp_err_t err = nvs_flash_init(); // Wi-Fi keeps its calibration data in NVS
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_events = xEventGroupCreate();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = false; // no stored connections: every game is set up from the menu
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
}

static void wifi_init_task(void *done)
{
    wifi_init();
    xSemaphoreGive(done);
    vTaskDelete(NULL);
}

// Initialises Wi-Fi once, then switches it to the given mode. The driver
// puts its interrupt on the core that initialises it, so that runs on core 0,
// away from Doom.
static void wifi_start(wifi_mode_t mode)
{
    static bool initialised;
    static esp_netif_t *ap, *sta;

    if (!initialised) {
        SemaphoreHandle_t done = xSemaphoreCreateBinary();
        xTaskCreatePinnedToCore(wifi_init_task, "wifi_init", 6144, done, 5, NULL, 0);
        xSemaphoreTake(done, portMAX_DELAY);
        vSemaphoreDelete(done);
        initialised = true;
    } else {
        esp_wifi_stop();
    }

    if (mode == WIFI_MODE_AP && !ap)
        ap = esp_netif_create_default_wifi_ap();
    if (mode == WIFI_MODE_STA && !sta)
        sta = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
    ESP_ERROR_CHECK(esp_wifi_start());
}

bool netplay_host(void)
{
    wifi_start(WIFI_MODE_AP);
    esp_wifi_stop(); // configure before the access point comes up

    wifi_config_t cfg = {
        .ap = {
            .channel = CHANNEL,
            .password = PASSWORD,
            .max_connection = MAX_CLIENTS,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    int len = snprintf((char *)cfg.ap.ssid, sizeof(cfg.ap.ssid), SSID_PREFIX "%s", netplay_game_name);
    cfg.ap.ssid_len = len;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_mode = WIFI_MODE_AP;
    snprintf(netplay_current_game, sizeof(netplay_current_game), "%s", netplay_game_name);
    // Lockstep netcode waits for every player's input each tic: keep the
    // radio awake so packets aren't held for the next beacon.
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "hosting \"%s\" on channel %d", cfg.ap.ssid, CHANNEL);
    return true;
}

int netplay_scan(netplay_game_t *games, int max)
{
    if (s_mode != WIFI_MODE_STA) {
        wifi_start(WIFI_MODE_STA);
        s_mode = WIFI_MODE_STA;
    }

    if (esp_wifi_scan_start(NULL, true) != ESP_OK)
        return 0;

    uint16_t n = 20;
    wifi_ap_record_t recs[20];
    esp_wifi_scan_get_ap_records(&n, recs);

    int count = 0;
    const size_t plen = strlen(SSID_PREFIX);
    for (int i = 0; i < n && count < max; i++) { // records come strongest first
        const char *ssid = (const char *)recs[i].ssid;
        if (strncmp(ssid, SSID_PREFIX, plen) != 0 || !ssid[plen])
            continue;
        bool dup = false;
        for (int j = 0; j < count; j++)
            dup |= strcmp(games[j].name, ssid + plen) == 0;
        if (dup)
            continue;
        snprintf(games[count].name, sizeof(games[count].name), "%s", ssid + plen);
        games[count].rssi = recs[i].rssi;
        count++;
    }
    return count;
}

bool netplay_join(const char *game_name, const char **server)
{
    wifi_config_t cfg = {
        .sta = {
            .password = PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), SSID_PREFIX "%s", game_name);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    for (int attempt = 0; attempt < 3; attempt++) {
        xEventGroupClearBits(s_events, EV_GOT_IP | EV_FAILED);
        esp_wifi_connect();
        EventBits_t bits = xEventGroupWaitBits(s_events, EV_GOT_IP | EV_FAILED, pdFALSE, pdFALSE,
                                               pdMS_TO_TICKS(10000));
        if (bits & EV_GOT_IP) {
            esp_wifi_set_ps(WIFI_PS_NONE);
            snprintf(netplay_current_game, sizeof(netplay_current_game), "%s", game_name);
            *server = s_server;
            return true;
        }
        ESP_LOGW(TAG, "joining \"%s\" failed, attempt %d", (char *)cfg.sta.ssid, attempt + 1);
    }
    return false;
}
