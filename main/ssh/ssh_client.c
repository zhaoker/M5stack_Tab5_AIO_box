/**
 * @file ssh_client.c
 * @brief SSH 客户端实现（LibSSH-ESP32 + FreeRTOS 接收任务）
 */
#include "ssh_client.h"

#include "libssh_esp32.h"
#include <libssh/libssh.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "ssh_client";

#define SSH_RECV_BUF_SIZE   4096
#define SSH_RECV_TIMEOUT_MS 100
#define SSH_TASK_STACK_SZ   8192
#define SSH_TASK_STACK_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define SSH_TASK_PRIORITY   5
#define SSH_KEEPALIVE_IDLE_MS 30000
/* Prefer mbedTLS backed host key types; libssh's bundled Ed25519 reference path is slow on ESP32-P4. */
#define SSH_HOSTKEY_PREF    "ecdsa-sha2-nistp256,rsa-sha2-256,rsa-sha2-512,ssh-rsa,ssh-ed25519"

struct ssh_client {
    ssh_session session;
    ssh_channel channel;
    bool running;
    bool connecting;
    bool cancel_requested;
    TaskHandle_t task;
    TickType_t last_activity_tick;
    ssh_data_cb_t data_cb;
    ssh_event_cb_t event_cb;
    void *user_data;
};

static ssh_client_t *s_default_client;

static void emit_event(ssh_client_t *client, ssh_event_t event, const char *reason)
{
    if (client && client->event_cb) {
        client->event_cb(event, reason, client->user_data);
    }
}

static void ssh_recv_task(void *param)
{
    ssh_client_t *client = (ssh_client_t *)param;
    char *buf = NULL;

    if (!client) {
        vTaskDeleteWithCaps(NULL);
        return;
    }
    buf = (char *)heap_caps_malloc(SSH_RECV_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (char *)heap_caps_malloc(SSH_RECV_BUF_SIZE, MALLOC_CAP_8BIT);
    }
    if (!buf) {
        ESP_LOGE(TAG, "Failed to alloc recv buffer");
        client->running = false;
        emit_event(client, SSH_EVT_ERROR, "Failed to alloc recv buffer");
        client->task = NULL;
        vTaskDeleteWithCaps(NULL);
        return;
    }

    while (client->running) {
        if (!client->channel ||
            ssh_channel_is_eof(client->channel) ||
            !ssh_channel_is_open(client->channel)) {
            ESP_LOGI(TAG, "Channel EOF/closed");
            break;
        }

        int n = ssh_channel_read_timeout(client->channel, buf, SSH_RECV_BUF_SIZE, 0,
                                         SSH_RECV_TIMEOUT_MS);
        if (n > 0) {
            client->last_activity_tick = xTaskGetTickCount();
            if (client->data_cb) {
                client->data_cb(buf, (size_t)n, client->user_data);
            }
        } else if (n == SSH_ERROR) {
            ESP_LOGE(TAG, "ssh_channel_read error: %s",
                     client->session ? ssh_get_error(client->session) : "no session");
            break;
        } else if ((xTaskGetTickCount() - client->last_activity_tick) >=
                   pdMS_TO_TICKS(SSH_KEEPALIVE_IDLE_MS)) {
            int rc = ssh_send_ignore(client->session, "tab5-keepalive");
            client->last_activity_tick = xTaskGetTickCount();
            if (rc != SSH_OK) {
                ESP_LOGW(TAG, "SSH keepalive failed: %s",
                         client->session ? ssh_get_error(client->session) : "no session");
                break;
            }
            ESP_LOGD(TAG, "SSH keepalive sent");
        }
    }

    bool cancelled = client->cancel_requested;
    client->running = false;
    free(buf);
    client->task = NULL;
    emit_event(client, SSH_EVT_DISCONNECTED,
               cancelled ? "closed" : NULL);
    ESP_LOGI(TAG, "SSH recv task exiting");
    vTaskDeleteWithCaps(NULL);
}

static int verify_knownhost(ssh_session session)
{
    enum ssh_known_hosts_e state = ssh_session_is_known_server(session);
    switch (state) {
        case SSH_KNOWN_HOSTS_OK:
            return 0;
        case SSH_KNOWN_HOSTS_NOT_FOUND:
        case SSH_KNOWN_HOSTS_UNKNOWN:
            ESP_LOGW(TAG, "Unknown host, accepting automatically");
            ssh_session_update_known_hosts(session);
            return 0;
        case SSH_KNOWN_HOSTS_CHANGED:
            ESP_LOGE(TAG, "Host key changed! Possible MITM attack.");
            return -1;
        case SSH_KNOWN_HOSTS_OTHER:
            ESP_LOGE(TAG, "Host key type changed.");
            return -1;
        case SSH_KNOWN_HOSTS_ERROR:
            ESP_LOGE(TAG, "Known hosts error: %s", ssh_get_error(session));
            return -1;
        default:
            return 0;
    }
}

ssh_client_t *ssh_client_create(void)
{
    return calloc(1, sizeof(ssh_client_t));
}

void ssh_client_destroy(ssh_client_t *client)
{
    if (!client) {
        return;
    }
    ssh_client_disconnect_handle(client);
    free(client);
}

esp_err_t ssh_client_connect_handle(ssh_client_t *client,
                                    const server_entry_t *cfg,
                                    int cols, int rows,
                                    ssh_data_cb_t data_cb,
                                    ssh_event_cb_t event_cb,
                                    void *user_data)
{
    if (!client || !cfg) {
        return ESP_ERR_INVALID_ARG;
    }

    if (client->running || client->connecting) {
        ESP_LOGW(TAG, "Already connected, disconnecting first");
        ssh_client_disconnect_handle(client);
        for (int i = 0; i < 30 && (client->running || client->connecting || client->task); i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    libssh_begin();

    client->data_cb = data_cb;
    client->event_cb = event_cb;
    client->user_data = user_data;
    client->connecting = true;
    client->cancel_requested = false;

    client->session = ssh_new();
    if (!client->session) {
        client->connecting = false;
        emit_event(client, SSH_EVT_ERROR, "Failed to create SSH session");
        return ESP_FAIL;
    }

    if (client->cancel_requested) {
        goto cleanup_cancelled;
    }

    int port = cfg->port ? (int)cfg->port : 22;
    int verbosity = SSH_LOG_NOLOG;

    ssh_options_set(client->session, SSH_OPTIONS_HOST, cfg->host);
    ssh_options_set(client->session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(client->session, SSH_OPTIONS_USER, cfg->username);
    ssh_options_set(client->session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    ssh_options_set(client->session, SSH_OPTIONS_HOSTKEYS, SSH_HOSTKEY_PREF);

    int rc = ssh_connect(client->session);
    if (client->cancel_requested) {
        goto cleanup_cancelled;
    }
    if (rc != SSH_OK) {
        char reason[192];
        snprintf(reason, sizeof(reason), "%s", ssh_get_error(client->session));
        ESP_LOGE(TAG, "ssh_connect failed: %s", reason);
        ssh_free(client->session);
        client->session = NULL;
        client->connecting = false;
        emit_event(client, SSH_EVT_ERROR, reason);
        return ESP_FAIL;
    }

    if (verify_knownhost(client->session) < 0) {
        if (client->cancel_requested) {
            goto cleanup_cancelled;
        }
        char reason[192];
        snprintf(reason, sizeof(reason), "%s", ssh_get_error(client->session));
        ssh_disconnect(client->session);
        ssh_free(client->session);
        client->session = NULL;
        client->connecting = false;
        emit_event(client, SSH_EVT_ERROR, reason);
        return ESP_FAIL;
    }

    rc = ssh_userauth_password(client->session, NULL, cfg->password);
    if (client->cancel_requested) {
        goto cleanup_cancelled;
    }
    if (rc != SSH_AUTH_SUCCESS) {
        char reason[192];
        snprintf(reason, sizeof(reason), "%s", ssh_get_error(client->session));
        ESP_LOGE(TAG, "Auth failed: %s", reason);
        ssh_disconnect(client->session);
        ssh_free(client->session);
        client->session = NULL;
        client->connecting = false;
        emit_event(client, SSH_EVT_AUTH_FAILED, reason);
        return ESP_ERR_INVALID_ARG;
    }

    client->channel = ssh_channel_new(client->session);
    if (!client->channel) {
        if (client->cancel_requested) {
            goto cleanup_cancelled;
        }
        emit_event(client, SSH_EVT_ERROR, "Failed to create SSH channel");
        ssh_disconnect(client->session);
        ssh_free(client->session);
        client->session = NULL;
        client->connecting = false;
        return ESP_FAIL;
    }

    rc = ssh_channel_open_session(client->channel);
    if (client->cancel_requested) {
        goto cleanup_cancelled;
    }
    if (rc != SSH_OK) {
        ESP_LOGE(TAG, "channel_open_session failed: %s", ssh_get_error(client->session));
        goto cleanup;
    }

    rc = ssh_channel_request_pty_size(client->channel, "xterm-256color", cols, rows);
    if (client->cancel_requested) {
        goto cleanup_cancelled;
    }
    if (rc != SSH_OK) {
        ESP_LOGE(TAG, "request_pty failed: %s", ssh_get_error(client->session));
        goto cleanup;
    }

    rc = ssh_channel_request_shell(client->channel);
    if (client->cancel_requested) {
        goto cleanup_cancelled;
    }
    if (rc != SSH_OK) {
        ESP_LOGE(TAG, "request_shell failed: %s", ssh_get_error(client->session));
        goto cleanup;
    }

    if (cfg->auto_cmd[0] != '\0') {
        char cmd_buf[sizeof(cfg->auto_cmd) + 2];
        int cmd_len = snprintf(cmd_buf, sizeof(cmd_buf), "%s\n", cfg->auto_cmd);
        vTaskDelay(pdMS_TO_TICKS(200));
        int written = ssh_channel_write(client->channel, cmd_buf, cmd_len);
        if (written == cmd_len) {
            ESP_LOGI(TAG, "Sent auto_cmd: %s", cfg->auto_cmd);
        } else {
            ESP_LOGW(TAG, "auto_cmd write incomplete: %d/%d", written, cmd_len);
        }
    }

    client->running = true;
    client->connecting = false;
    client->last_activity_tick = xTaskGetTickCount();
    if (xTaskCreateWithCaps(ssh_recv_task, "ssh_recv", SSH_TASK_STACK_SZ, client,
                            SSH_TASK_PRIORITY, &client->task,
                            SSH_TASK_STACK_CAPS) != pdPASS) {
        ESP_LOGE(TAG,
                 "Failed to create recv task stack=%u caps=0x%x internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u",
                 (unsigned)SSH_TASK_STACK_SZ,
                 (unsigned)SSH_TASK_STACK_CAPS,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        client->running = false;
        client->connecting = false;
        goto cleanup;
    }

    emit_event(client, SSH_EVT_CONNECTED, NULL);
    ESP_LOGI(TAG, "SSH connected to %s@%s:%d", cfg->username, cfg->host, port);
    return ESP_OK;

cleanup:
    {
        char reason[192];
        snprintf(reason, sizeof(reason), "%s",
                 client->session ? ssh_get_error(client->session) : "SSH error");
        emit_event(client, SSH_EVT_ERROR, reason);
    }
    if (client->channel) {
        ssh_channel_free(client->channel);
        client->channel = NULL;
    }
    if (client->session) {
        ssh_disconnect(client->session);
        ssh_free(client->session);
        client->session = NULL;
    }
    client->connecting = false;
    return ESP_FAIL;

cleanup_cancelled:
    if (client->channel) {
        ssh_channel_free(client->channel);
        client->channel = NULL;
    }
    if (client->session) {
        ssh_disconnect(client->session);
        ssh_free(client->session);
        client->session = NULL;
    }
    client->running = false;
    client->connecting = false;
    return ESP_ERR_INVALID_STATE;
}

int ssh_client_write_handle(ssh_client_t *client, const char *data, size_t len)
{
    if (!client || !client->running || !client->channel || !data || len == 0) {
        return -1;
    }
    int written = ssh_channel_write(client->channel, data, (uint32_t)len);
    if (written > 0) {
        client->last_activity_tick = xTaskGetTickCount();
    }
    return written;
}

void ssh_client_resize_handle(ssh_client_t *client, int cols, int rows)
{
    if (!client || !client->running || !client->channel) {
        return;
    }
    ssh_channel_change_pty_size(client->channel, cols, rows);
}

void ssh_client_disconnect_handle(ssh_client_t *client)
{
    if (!client) {
        return;
    }

    client->running = false;
    client->cancel_requested = true;

    if (client->connecting) {
        ESP_LOGW(TAG, "Disconnect requested while connecting; deferring cleanup");
        return;
    }

    for (int i = 0; i < 20 && client->task; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (client->task) {
        ESP_LOGW(TAG, "SSH recv task still running; postpone channel cleanup");
        return;
    }

    if (client->channel) {
        ssh_channel_send_eof(client->channel);
        ssh_channel_close(client->channel);
        ssh_channel_free(client->channel);
        client->channel = NULL;
    }
    if (client->session) {
        ssh_disconnect(client->session);
        ssh_free(client->session);
        client->session = NULL;
    }
    client->cancel_requested = false;
    ESP_LOGI(TAG, "SSH disconnected");
}

bool ssh_client_is_connected_handle(const ssh_client_t *client)
{
    return client && client->running;
}

esp_err_t ssh_client_connect(const server_entry_t *cfg,
                             int cols, int rows,
                             ssh_data_cb_t data_cb,
                             ssh_event_cb_t event_cb,
                             void *user_data)
{
    if (!s_default_client) {
        s_default_client = ssh_client_create();
        if (!s_default_client) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ssh_client_connect_handle(s_default_client, cfg, cols, rows,
                                     data_cb, event_cb, user_data);
}

int ssh_client_write(const char *data, size_t len)
{
    return ssh_client_write_handle(s_default_client, data, len);
}

void ssh_client_resize(int cols, int rows)
{
    ssh_client_resize_handle(s_default_client, cols, rows);
}

void ssh_client_disconnect(void)
{
    ssh_client_disconnect_handle(s_default_client);
}

bool ssh_client_is_connected(void)
{
    return ssh_client_is_connected_handle(s_default_client);
}
