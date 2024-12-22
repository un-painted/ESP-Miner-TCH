#include "esp_log.h"
// #include "addr_from_stdin.h"
#include "bm1397.h"
#include "connect.h"
#include "system.h"
#include "global_state.h"
#include "lwip/dns.h"
#include <lwip/tcpip.h>
#include "nvs_config.h"
#include "stratum_task.h"
#include "work_queue.h"
#include "esp_wifi.h"
#include <esp_sntp.h>
#include <time.h>

#define PORT CONFIG_STRATUM_PORT
#define STRATUM_URL CONFIG_STRATUM_URL

#define FALLBACK_PORT CONFIG_FALLBACK_STRATUM_PORT
#define FALLBACK_STRATUM_URL CONFIG_FALLBACK_STRATUM_URL

#define STRATUM_PW CONFIG_STRATUM_PW
#define FALLBACK_STRATUM_PW CONFIG_FALLBACK_STRATUM_PW
#define STRATUM_DIFFICULTY CONFIG_STRATUM_DIFFICULTY

#define MAX_RETRY_ATTEMPTS 3
#define MAX_CRITICAL_RETRY_ATTEMPTS 5

static const char * TAG = "stratum_task";

static StratumApiV1Message stratum_api_v1_message = {};
static SystemTaskModule SYSTEM_TASK_MODULE = {.stratum_difficulty = 1024};

static const char * primary_stratum_url;
static uint16_t primary_stratum_port;

bool is_wifi_connected() {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return true;
    } else {
        return false;
    }
}

void cleanQueue(GlobalState * GLOBAL_STATE) {
    ESP_LOGI(TAG, "Clean Jobs: clearing queue");
    GLOBAL_STATE->abandon_work = 1;
    queue_clear(&GLOBAL_STATE->stratum_queue);

    pthread_mutex_lock(&GLOBAL_STATE->valid_jobs_lock);
    ASIC_jobs_queue_clear(&GLOBAL_STATE->ASIC_jobs_queue);
    for (int i = 0; i < 128; i = i + 4) {
        GLOBAL_STATE->valid_jobs[i] = 0;
    }
    pthread_mutex_unlock(&GLOBAL_STATE->valid_jobs_lock);
}

void stratum_close_connection(GlobalState * GLOBAL_STATE)
{
    if (GLOBAL_STATE->sock < 0) {
        ESP_LOGE(TAG, "Socket already shutdown, not shutting down again..");
        return;
    }

    ESP_LOGE(TAG, "Shutting down socket and restarting...");
    shutdown(GLOBAL_STATE->sock, SHUT_RDWR);
    close(GLOBAL_STATE->sock);
    cleanQueue(GLOBAL_STATE);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

void stratum_primary_heartbeat(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    ESP_LOGI(TAG, "Starting heartbeat thread for primary endpoint: %s", primary_stratum_url);
    vTaskDelay(10000 / portTICK_PERIOD_MS);

    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;

    while (1)
    {
        if (GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback == false) {
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        char host_ip[INET_ADDRSTRLEN];
        ESP_LOGD(TAG, "Running Heartbeat on: %s!", primary_stratum_url);

        if (!is_wifi_connected()) {
            ESP_LOGD(TAG, "Heartbeat. Failed WiFi check!");
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        struct hostent *primary_dns_addr = gethostbyname(primary_stratum_url);
        if (primary_dns_addr == NULL) {
            ESP_LOGD(TAG, "Heartbeat. Failed DNS check for: %s!", primary_stratum_url);
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }
        inet_ntop(AF_INET, (void *)primary_dns_addr->h_addr_list[0], host_ip, sizeof(host_ip));

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(primary_stratum_port);

        int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGD(TAG, "Heartbeat. Failed socket create check!");
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            close(sock);
            continue;
        }

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        if (err != 0)
        {
            ESP_LOGD(TAG, "Heartbeat. Failed connect check: %s:%d (errno %d: %s)", host_ip, primary_stratum_port, errno, strerror(errno));
            close(sock);
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }
        shutdown(sock, SHUT_RDWR);
        close(sock);

        if (GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback) {
            ESP_LOGI(TAG, "Heartbeat successful and in fallback mode. Switching back to primary.");
            GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = false;
            stratum_close_connection(GLOBAL_STATE);
            vTaskDelay(60000 / portTICK_PERIOD_MS);
            continue;
        }
        vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}

void stratum_task(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;

    primary_stratum_url = GLOBAL_STATE->SYSTEM_MODULE.pool_url;
    primary_stratum_port = GLOBAL_STATE->SYSTEM_MODULE.pool_port;
    char * stratum_url = GLOBAL_STATE->SYSTEM_MODULE.pool_url;
    uint16_t port = GLOBAL_STATE->SYSTEM_MODULE.pool_port;

    STRATUM_V1_initialize_buffer();
    char host_ip[20];
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    int retry_attempts = 0;
    int retry_critical_attempts = 0;
    struct timeval timeout = {};
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    xTaskCreate(stratum_primary_heartbeat, "stratum primary heartbeat", 4096, pvParameters, 1, NULL);

    ESP_LOGI(TAG, "Trying to get IP for URL: %s", stratum_url);
    while (1) {
        if (!is_wifi_connected()) {
            ESP_LOGI(TAG, "WiFi disconnected, attempting to reconnect...");
            esp_wifi_connect();
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            continue;
        }

        if (retry_attempts >= MAX_RETRY_ATTEMPTS)
        {
            if (GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_url == NULL || GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_url[0] == '\0') {
                ESP_LOGI(TAG, "Unable to switch to fallback. No url configured. (retries: %d)...", retry_attempts);
                GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = false;
                retry_attempts = 0;
                continue;
            }

            GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback = !GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback;
            ESP_LOGI(TAG, "Switching target due to too many failures (retries: %d)...", retry_attempts);
            retry_attempts = 0;
        }

        stratum_url = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_url : GLOBAL_STATE->SYSTEM_MODULE.pool_url;
        port = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? GLOBAL_STATE->SYSTEM_MODULE.fallback_pool_port : GLOBAL_STATE->SYSTEM_MODULE.pool_port;

        struct hostent *dns_addr = gethostbyname(stratum_url);
        if (dns_addr == NULL) {
            retry_attempts++;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        inet_ntop(AF_INET, (void *)dns_addr->h_addr_list[0], host_ip, sizeof(host_ip));

        ESP_LOGI(TAG, "Connecting to: stratum+tcp://%s:%d (%s)", stratum_url, port, host_ip);

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(port);

        GLOBAL_STATE->sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (GLOBAL_STATE->sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            if (++retry_critical_attempts > MAX_CRITICAL_RETRY_ATTEMPTS) {
                ESP_LOGE(TAG, "Max retry attempts reached, restarting...");
                esp_restart();
            }
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }
        retry_critical_attempts = 0;

        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, port);
        int err = connect(GLOBAL_STATE->sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in6));
        if (err != 0)
        {
            retry_attempts++;
            ESP_LOGE(TAG, "Socket unable to connect to %s:%d (errno %d: %s)", stratum_url, port, errno, strerror(errno));
            // close the socket
            shutdown(GLOBAL_STATE->sock, SHUT_RDWR);
            close(GLOBAL_STATE->sock);
            // instead of restarting, retry this every 5 seconds
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }
        retry_attempts = 0;

        if (setsockopt(GLOBAL_STATE->sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
            ESP_LOGE(TAG, "Fail to setsockopt SO_SNDTIMEO");
        }

        STRATUM_V1_reset_uid();
        cleanQueue(GLOBAL_STATE);

        ///// Start Stratum Action
        // mining.subscribe - ID: 1
        STRATUM_V1_subscribe(GLOBAL_STATE->sock, GLOBAL_STATE->device_model_str, GLOBAL_STATE->asic_model_str);

        // mining.configure - ID: 2
        STRATUM_V1_configure_version_rolling(GLOBAL_STATE->sock, &GLOBAL_STATE->version_mask);

        char * username = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_USER, FALLBACK_STRATUM_USER) : nvs_config_get_string(NVS_CONFIG_STRATUM_USER, STRATUM_USER);
        char * password = GLOBAL_STATE->SYSTEM_MODULE.is_using_fallback ? nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_PASS, FALLBACK_STRATUM_PW) : nvs_config_get_string(NVS_CONFIG_STRATUM_PASS, STRATUM_PW);

        //mining.authorize - ID: 3
        STRATUM_V1_authenticate(GLOBAL_STATE->sock, username, password);
        free(password);
        free(username);

        //mining.suggest_difficulty - ID: 4
        STRATUM_V1_suggest_difficulty(GLOBAL_STATE->sock, STRATUM_DIFFICULTY);

        while (1) {
            char * line = STRATUM_V1_receive_jsonrpc_line(GLOBAL_STATE->sock);
            if (!line) {
                ESP_LOGE(TAG, "Failed to receive JSON-RPC line, reconnecting...");
                stratum_close_connection(GLOBAL_STATE);
                break;
            }
            ESP_LOGI(TAG, "rx: %s", line); // debug incoming stratum messages
            STRATUM_V1_parse(&stratum_api_v1_message, line);
            free(line);

            if (stratum_api_v1_message.method == MINING_NOTIFY) {
                SYSTEM_notify_new_ntime(GLOBAL_STATE, stratum_api_v1_message.mining_notification->ntime);
                if (stratum_api_v1_message.should_abandon_work &&
                    (GLOBAL_STATE->stratum_queue.count > 0 || GLOBAL_STATE->ASIC_jobs_queue.count > 0)) {
                    cleanQueue(GLOBAL_STATE);
                }
                if (GLOBAL_STATE->stratum_queue.count == QUEUE_SIZE) {
                    mining_notify * next_notify_json_str = (mining_notify *) queue_dequeue(&GLOBAL_STATE->stratum_queue);
                    STRATUM_V1_free_mining_notify(next_notify_json_str);
                }
                stratum_api_v1_message.mining_notification->difficulty = SYSTEM_TASK_MODULE.stratum_difficulty;
                queue_enqueue(&GLOBAL_STATE->stratum_queue, stratum_api_v1_message.mining_notification);
            } else if (stratum_api_v1_message.method == MINING_SET_DIFFICULTY) {
                if (stratum_api_v1_message.new_difficulty != SYSTEM_TASK_MODULE.stratum_difficulty) {
                    SYSTEM_TASK_MODULE.stratum_difficulty = stratum_api_v1_message.new_difficulty;
                    ESP_LOGI(TAG, "Set stratum difficulty: %ld", SYSTEM_TASK_MODULE.stratum_difficulty);
                }
            } else if (stratum_api_v1_message.method == MINING_SET_VERSION_MASK ||
                    stratum_api_v1_message.method == STRATUM_RESULT_VERSION_MASK) {
                // 1fffe000
                ESP_LOGI(TAG, "Set version mask: %08lx", stratum_api_v1_message.version_mask);
                GLOBAL_STATE->version_mask = stratum_api_v1_message.version_mask;
                GLOBAL_STATE->new_stratum_version_rolling_msg = true;
            } else if (stratum_api_v1_message.method == STRATUM_RESULT_SUBSCRIBE) {
                GLOBAL_STATE->extranonce_str = stratum_api_v1_message.extranonce_str;
                GLOBAL_STATE->extranonce_2_len = stratum_api_v1_message.extranonce_2_len;
            } else if (stratum_api_v1_message.method == CLIENT_RECONNECT) {
                ESP_LOGE(TAG, "Pool requested client reconnect...");
                stratum_close_connection(GLOBAL_STATE);
                break;
            } else if (stratum_api_v1_message.method == STRATUM_RESULT) {
                if (stratum_api_v1_message.response_success) {
                    ESP_LOGI(TAG, "message result accepted");
                    SYSTEM_notify_accepted_share(GLOBAL_STATE);
                } else {
                    ESP_LOGW(TAG, "message result rejected");
                    SYSTEM_notify_rejected_share(GLOBAL_STATE);
                }
            } else if (stratum_api_v1_message.method == STRATUM_RESULT_SETUP) {
                if (stratum_api_v1_message.response_success) {
                    ESP_LOGI(TAG, "setup message accepted");
                } else {
                    ESP_LOGE(TAG, "setup message rejected");
                }
            }
        }
    }
    vTaskDelete(NULL);
}