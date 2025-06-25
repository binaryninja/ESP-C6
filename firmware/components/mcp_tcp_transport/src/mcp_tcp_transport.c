/**
 * @file mcp_tcp_transport.c
 * @brief ESP32-C6 MCP TCP Transport Implementation
 * 
 * This component provides TCP transport for the Model Context Protocol (MCP) server
 * running on ESP32-C6. It creates a TCP server on port 8080 when WiFi is connected
 * and handles JSON-RPC communication with MCP clients.
 */

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "mcp_tcp_transport.h"
#include "mcp_server_simple.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "cJSON.h"

static const char *TAG = "mcp_tcp_transport";

/**
 * @brief Client connection structure
 */
typedef struct {
    int socket;                         ///< Client socket descriptor
    uint32_t client_id;                 ///< Unique client identifier
    struct sockaddr_in addr;            ///< Client address
    bool connected;                     ///< Connection status
    uint64_t connect_time;              ///< Connection timestamp
    uint32_t messages_received;         ///< Messages received from this client
    uint32_t messages_sent;             ///< Messages sent to this client
} mcp_tcp_client_t;

/**
 * @brief MCP TCP Transport Structure
 */
typedef struct {
    mcp_tcp_transport_config_t config;  ///< Transport configuration
    mcp_tcp_transport_status_t status;  ///< Current status
    mcp_tcp_transport_stats_t stats;    ///< Transport statistics
    
    int server_socket;                  ///< Server socket descriptor
    TaskHandle_t server_task;           ///< Server task handle
    TaskHandle_t client_tasks[4];       ///< Client handler task handles
    SemaphoreHandle_t mutex;            ///< Synchronization mutex
    
    mcp_tcp_client_t clients[4];        ///< Client connections
    uint8_t client_count;               ///< Active client count
    uint32_t next_client_id;            ///< Next client ID to assign
    
    void *mcp_server_handle;            ///< Associated MCP server handle
    uint64_t start_time;                ///< Transport start time
    bool initialized;                   ///< Initialization status
    bool running;                       ///< Running status
} mcp_tcp_transport_t;

/* Forward declarations */
static void mcp_tcp_server_task(void *arg);
static void mcp_tcp_client_task(void *arg);
static esp_err_t handle_client_message(mcp_tcp_transport_t *transport, 
                                       mcp_tcp_client_t *client, 
                                       const char *message, 
                                       size_t message_len);
static esp_err_t send_client_response(mcp_tcp_client_t *client, 
                                      const char *response, 
                                      size_t response_len);
static void cleanup_client(mcp_tcp_transport_t *transport, mcp_tcp_client_t *client);
static int find_free_client_slot(mcp_tcp_transport_t *transport);

/* Initialize MCP TCP Transport */
esp_err_t mcp_tcp_transport_init(const mcp_tcp_transport_config_t *config, 
                                 mcp_tcp_transport_handle_t *transport_handle)
{
    if (!config || !transport_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing MCP TCP transport on port %d", config->server_port);
    
    /* Allocate transport structure */
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)heap_caps_calloc(1, 
                                     sizeof(mcp_tcp_transport_t), MALLOC_CAP_DEFAULT);
    if (!transport) {
        ESP_LOGE(TAG, "Failed to allocate transport structure");
        return ESP_ERR_NO_MEM;
    }
    
    /* Copy configuration */
    memcpy(&transport->config, config, sizeof(mcp_tcp_transport_config_t));
    
    /* Create synchronization objects */
    transport->mutex = xSemaphoreCreateMutex();
    if (!transport->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(transport);
        return ESP_ERR_NO_MEM;
    }
    
    /* Initialize client array */
    for (int i = 0; i < transport->config.max_clients; i++) {
        transport->clients[i].socket = -1;
        transport->clients[i].connected = false;
        transport->client_tasks[i] = NULL;
    }
    
    /* Initialize statistics */
    memset(&transport->stats, 0, sizeof(mcp_tcp_transport_stats_t));
    transport->server_socket = -1;
    transport->status = MCP_TCP_STATUS_STOPPED;
    transport->next_client_id = 1;
    transport->initialized = true;
    
    *transport_handle = transport;
    
    ESP_LOGI(TAG, "MCP TCP transport initialized successfully");
    return ESP_OK;
}

/* Start MCP TCP Transport */
esp_err_t mcp_tcp_transport_start(mcp_tcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)transport_handle;
    
    if (!transport->initialized) {
        ESP_LOGE(TAG, "Transport not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (transport->running) {
        ESP_LOGW(TAG, "Transport already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting MCP TCP transport server on port %d", transport->config.server_port);
    
    transport->status = MCP_TCP_STATUS_STARTING;
    
    /* Create server task */
    BaseType_t task_ret = xTaskCreate(mcp_tcp_server_task, 
                                     "mcp_tcp_server", 
                                     transport->config.task_stack_size / sizeof(StackType_t),
                                     transport, 
                                     transport->config.task_priority, 
                                     &transport->server_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create server task");
        transport->status = MCP_TCP_STATUS_ERROR;
        return ESP_ERR_NO_MEM;
    }
    
    transport->running = true;
    transport->start_time = esp_timer_get_time();
    
    ESP_LOGI(TAG, "MCP TCP transport started successfully");
    return ESP_OK;
}

/* Stop MCP TCP Transport */
esp_err_t mcp_tcp_transport_stop(mcp_tcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)transport_handle;
    
    if (!transport->running) {
        ESP_LOGW(TAG, "Transport not running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping MCP TCP transport");
    
    transport->running = false;
    transport->status = MCP_TCP_STATUS_STOPPED;
    
    /* Close server socket */
    if (transport->server_socket >= 0) {
        close(transport->server_socket);
        transport->server_socket = -1;
    }
    
    /* Clean up all clients */
    if (xSemaphoreTake(transport->mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (int i = 0; i < transport->config.max_clients; i++) {
            if (transport->clients[i].connected) {
                cleanup_client(transport, &transport->clients[i]);
            }
        }
        xSemaphoreGive(transport->mutex);
    }
    
    /* Wait for server task to finish */
    if (transport->server_task) {
        // Task will delete itself
        transport->server_task = NULL;
    }
    
    ESP_LOGI(TAG, "MCP TCP transport stopped");
    return ESP_OK;
}

/* Deinitialize MCP TCP Transport */
esp_err_t mcp_tcp_transport_deinit(mcp_tcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)transport_handle;
    
    /* Stop transport if running */
    if (transport->running) {
        mcp_tcp_transport_stop(transport_handle);
    }
    
    /* Clean up synchronization objects */
    if (transport->mutex) {
        vSemaphoreDelete(transport->mutex);
    }
    
    /* Free transport structure */
    free(transport);
    
    ESP_LOGI(TAG, "MCP TCP transport deinitialized");
    return ESP_OK;
}

/* Get Transport Status */
mcp_tcp_transport_status_t mcp_tcp_transport_get_status(mcp_tcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return MCP_TCP_STATUS_ERROR;
    }
    
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)transport_handle;
    return transport->status;
}

/* Get Transport Statistics */
esp_err_t mcp_tcp_transport_get_stats(mcp_tcp_transport_handle_t transport_handle, 
                                      mcp_tcp_transport_stats_t *stats)
{
    if (!transport_handle || !stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)transport_handle;
    
    if (xSemaphoreTake(transport->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &transport->stats, sizeof(mcp_tcp_transport_stats_t));
        if (transport->running) {
            stats->uptime_ms = (esp_timer_get_time() - transport->start_time) / 1000;
        }
        xSemaphoreGive(transport->mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

/* Check if Transport is Running */
bool mcp_tcp_transport_is_running(mcp_tcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return false;
    }
    
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)transport_handle;
    return transport->running && (transport->status == MCP_TCP_STATUS_LISTENING);
}

/* Set MCP Server Handle */
esp_err_t mcp_tcp_transport_set_mcp_server(mcp_tcp_transport_handle_t transport_handle,
                                           void *mcp_server_handle)
{
    if (!transport_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)transport_handle;
    transport->mcp_server_handle = mcp_server_handle;
    
    ESP_LOGI(TAG, "MCP server handle associated with TCP transport");
    return ESP_OK;
}

/* Get Server Port */
uint16_t mcp_tcp_transport_get_port(mcp_tcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return 0;
    }
    
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)transport_handle;
    return transport->config.server_port;
}

/* Get Active Client Count */
uint8_t mcp_tcp_transport_get_client_count(mcp_tcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return 0;
    }
    
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)transport_handle;
    return transport->client_count;
}

/* Send Message to Client */
esp_err_t mcp_tcp_transport_send_message(mcp_tcp_transport_handle_t transport_handle,
                                         uint32_t client_id,
                                         const char *message,
                                         size_t message_len)
{
    if (!transport_handle || !message || message_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)transport_handle;
    
    if (xSemaphoreTake(transport->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < transport->config.max_clients; i++) {
            if (transport->clients[i].connected && transport->clients[i].client_id == client_id) {
                esp_err_t ret = send_client_response(&transport->clients[i], message, message_len);
                xSemaphoreGive(transport->mutex);
                return ret;
            }
        }
        xSemaphoreGive(transport->mutex);
    }
    
    return ESP_ERR_NOT_FOUND;
}

/* Broadcast Message to All Clients */
esp_err_t mcp_tcp_transport_broadcast_message(mcp_tcp_transport_handle_t transport_handle,
                                              const char *message,
                                              size_t message_len)
{
    if (!transport_handle || !message || message_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)transport_handle;
    esp_err_t result = ESP_OK;
    
    if (xSemaphoreTake(transport->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < transport->config.max_clients; i++) {
            if (transport->clients[i].connected) {
                esp_err_t ret = send_client_response(&transport->clients[i], message, message_len);
                if (ret != ESP_OK) {
                    result = ret;
                }
            }
        }
        xSemaphoreGive(transport->mutex);
    } else {
        result = ESP_ERR_TIMEOUT;
    }
    
    return result;
}

/* Reset Transport Statistics */
void mcp_tcp_transport_reset_stats(mcp_tcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return;
    }
    
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)transport_handle;
    
    if (xSemaphoreTake(transport->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&transport->stats, 0, sizeof(mcp_tcp_transport_stats_t));
        transport->start_time = esp_timer_get_time();
        
        /* Reset client statistics */
        for (int i = 0; i < transport->config.max_clients; i++) {
            transport->clients[i].messages_received = 0;
            transport->clients[i].messages_sent = 0;
        }
        
        xSemaphoreGive(transport->mutex);
    }
    
    ESP_LOGI(TAG, "Transport statistics reset");
}

/* TCP Server Task */
static void mcp_tcp_server_task(void *arg)
{
    mcp_tcp_transport_t *transport = (mcp_tcp_transport_t*)arg;
    
    ESP_LOGI(TAG, "MCP TCP server task started on port %d", transport->config.server_port);
    
    /* Create server socket */
    transport->server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (transport->server_socket < 0) {
        ESP_LOGE(TAG, "Failed to create server socket: %s", strerror(errno));
        transport->status = MCP_TCP_STATUS_ERROR;
        goto cleanup;
    }
    
    /* Set socket options */
    int opt = 1;
    if (setsockopt(transport->server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ESP_LOGW(TAG, "Failed to set SO_REUSEADDR: %s", strerror(errno));
    }
    
    /* Set keep-alive options */
    if (setsockopt(transport->server_socket, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
        ESP_LOGW(TAG, "Failed to set SO_KEEPALIVE: %s", strerror(errno));
    }
    
    /* Bind server socket */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(transport->config.server_port);
    
    if (bind(transport->server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind server socket: %s", strerror(errno));
        transport->status = MCP_TCP_STATUS_ERROR;
        goto cleanup;
    }
    
    /* Listen for connections */
    if (listen(transport->server_socket, transport->config.max_clients) < 0) {
        ESP_LOGE(TAG, "Failed to listen on server socket: %s", strerror(errno));
        transport->status = MCP_TCP_STATUS_ERROR;
        goto cleanup;
    }
    
    transport->status = MCP_TCP_STATUS_LISTENING;
    ESP_LOGI(TAG, "MCP TCP server listening on port %d", transport->config.server_port);
    
    /* Accept client connections */
    while (transport->running) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        int client_socket = accept(transport->server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            if (transport->running) {
                ESP_LOGE(TAG, "Failed to accept client connection: %s", strerror(errno));
                transport->stats.errors++;
            }
            continue;
        }
        
        /* Find free client slot */
        if (xSemaphoreTake(transport->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            int slot = find_free_client_slot(transport);
            if (slot >= 0) {
                /* Initialize client */
                mcp_tcp_client_t *client = &transport->clients[slot];
                client->socket = client_socket;
                client->client_id = transport->next_client_id++;
                memcpy(&client->addr, &client_addr, sizeof(client_addr));
                client->connected = true;
                client->connect_time = esp_timer_get_time();
                client->messages_received = 0;
                client->messages_sent = 0;
                
                transport->client_count++;
                transport->stats.total_connections++;
                transport->stats.active_connections++;
                
                ESP_LOGI(TAG, "Client %lu connected from %s:%d", 
                         (unsigned long)client->client_id,
                         inet_ntoa(client_addr.sin_addr), 
                         ntohs(client_addr.sin_port));
                
                /* Create client handler task */
                char task_name[16];
                snprintf(task_name, sizeof(task_name), "mcp_client_%lu", (unsigned long)client->client_id);
                
                BaseType_t task_ret = xTaskCreate(mcp_tcp_client_task, 
                                                 task_name,
                                                 4096 / sizeof(StackType_t),
                                                 client, 
                                                 transport->config.task_priority - 1, 
                                                 &transport->client_tasks[slot]);
                if (task_ret != pdPASS) {
                    ESP_LOGE(TAG, "Failed to create client task for client %lu", (unsigned long)client->client_id);
                    cleanup_client(transport, client);
                } else {
                    ESP_LOGI(TAG, "Client handler task created for client %lu", (unsigned long)client->client_id);
                }
            } else {
                ESP_LOGW(TAG, "Maximum clients reached, rejecting connection");
                close(client_socket);
                transport->stats.errors++;
            }
            xSemaphoreGive(transport->mutex);
        } else {
            ESP_LOGE(TAG, "Failed to acquire mutex for client connection");
            close(client_socket);
            transport->stats.errors++;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to prevent tight loop
    }
    
cleanup:
    if (transport->server_socket >= 0) {
        close(transport->server_socket);
        transport->server_socket = -1;
    }
    
    transport->status = MCP_TCP_STATUS_STOPPED;
    ESP_LOGI(TAG, "MCP TCP server task stopped");
    vTaskDelete(NULL);
}

/* TCP Client Task */
static void mcp_tcp_client_task(void *arg)
{
    mcp_tcp_client_t *client = (mcp_tcp_client_t*)arg;
    char *buffer = malloc(2048);
    
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate client buffer");
        return;
    }
    
    ESP_LOGI(TAG, "Client handler task started for client %lu", (unsigned long)client->client_id);
    
    while (client->connected) {
        int bytes_received = recv(client->socket, buffer, 2047, 0);
        
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                ESP_LOGI(TAG, "Client %lu disconnected", (unsigned long)client->client_id);
            } else {
                ESP_LOGE(TAG, "Client %lu receive error: %s", (unsigned long)client->client_id, strerror(errno));
            }
            break;
        }
        
        buffer[bytes_received] = '\0';
        
        ESP_LOGI(TAG, "Received %d bytes from client %lu: %s", 
                 bytes_received, (unsigned long)client->client_id, buffer);
        
        /* Find the transport structure (we need to pass it somehow) */
        /* For now, we'll handle the message directly here */
        /* This is a simplified implementation - in production you'd want better separation */
        
        /* Parse JSON and handle MCP request */
        cJSON *json = cJSON_Parse(buffer);
        if (json) {
            /* Create a simple response */
            cJSON *response = cJSON_CreateObject();
            cJSON *id = cJSON_GetObjectItem(json, "id");
            if (id) {
                cJSON_AddItemToObject(response, "id", cJSON_Duplicate(id, 1));
            }
            cJSON_AddStringToObject(response, "jsonrpc", "2.0");
            
            cJSON *method = cJSON_GetObjectItem(json, "method");
            if (method && cJSON_IsString(method)) {
                if (strcmp(method->valuestring, "ping") == 0) {
                    cJSON_AddStringToObject(response, "result", "pong");
                } else if (strcmp(method->valuestring, "tools/list") == 0) {
                    cJSON *result = cJSON_CreateObject();
                    cJSON *tools = cJSON_CreateArray();
                    
                    /* Add sample tools */
                    cJSON *echo_tool = cJSON_CreateObject();
                    cJSON_AddStringToObject(echo_tool, "name", "echo");
                    cJSON_AddStringToObject(echo_tool, "description", "Echo input text");
                    cJSON_AddItemToArray(tools, echo_tool);
                    
                    cJSON *display_tool = cJSON_CreateObject();
                    cJSON_AddStringToObject(display_tool, "name", "display_control");
                    cJSON_AddStringToObject(display_tool, "description", "Control ST7789 display");
                    cJSON_AddItemToArray(tools, display_tool);
                    
                    cJSON_AddItemToObject(result, "tools", tools);
                    cJSON_AddItemToObject(response, "result", result);
                } else {
                    cJSON *error = cJSON_CreateObject();
                    cJSON_AddNumberToObject(error, "code", -32601);
                    cJSON_AddStringToObject(error, "message", "Method not found");
                    cJSON_AddItemToObject(response, "error", error);
                }
            } else {
                cJSON *error = cJSON_CreateObject();
                cJSON_AddNumberToObject(error, "code", -32600);
                cJSON_AddStringToObject(error, "message", "Invalid Request");
                cJSON_AddItemToObject(response, "error", error);
            }
            
            char *response_str = cJSON_Print(response);
            if (response_str) {
                /* Add newline for easier parsing */
                char *response_with_newline = malloc(strlen(response_str) + 2);
                if (response_with_newline) {
                    sprintf(response_with_newline, "%s\n", response_str);
                    send_client_response(client, response_with_newline, strlen(response_with_newline));
                    free(response_with_newline);
                }
                free(response_str);
            }
            
            cJSON_Delete(response);
            cJSON_Delete(json);
        } else {
            ESP_LOGW(TAG, "Failed to parse JSON from client %lu", (unsigned long)client->client_id);
            
            /* Send error response */
            const char *error_response = "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32700,\"message\":\"Parse error\"},\"id\":null}\n";
            send_client_response(client, error_response, strlen(error_response));
        }
        
        client->messages_received++;
    }
    
    /* Cleanup client */
    ESP_LOGI(TAG, "Cleaning up client %lu", (unsigned long)client->client_id);
    close(client->socket);
    client->connected = false;
    
    free(buffer);
    ESP_LOGI(TAG, "Client handler task finished for client %lu", (unsigned long)client->client_id);
    vTaskDelete(NULL);
}

/* Handle Client Message */
static esp_err_t handle_client_message(mcp_tcp_transport_t *transport, 
                                       mcp_tcp_client_t *client, 
                                       const char *message, 
                                       size_t message_len)
{
    ESP_LOGI(TAG, "Processing message from client %lu: %.*s", 
             (unsigned long)client->client_id, (int)message_len, message);
    
    /* Update statistics */
    transport->stats.messages_received++;
    transport->stats.bytes_received += message_len;
    client->messages_received++;
    
    /* TODO: Integrate with actual MCP server */
    /* For now, send a simple acknowledgment */
    const char *ack = "{\"jsonrpc\":\"2.0\",\"result\":\"Message received\",\"id\":1}\n";
    return send_client_response(client, ack, strlen(ack));
}

/* Send Client Response */
static esp_err_t send_client_response(mcp_tcp_client_t *client, 
                                      const char *response, 
                                      size_t response_len)
{
    if (!client->connected || client->socket < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int bytes_sent = send(client->socket, response, response_len, 0);
    if (bytes_sent < 0) {
        ESP_LOGE(TAG, "Failed to send response to client %lu: %s", 
                 (unsigned long)client->client_id, strerror(errno));
        return ESP_FAIL;
    }
    
    if (bytes_sent != response_len) {
        ESP_LOGW(TAG, "Partial send to client %lu: %d/%d bytes", 
                 (unsigned long)client->client_id, bytes_sent, (int)response_len);
    }
    
    client->messages_sent++;
    ESP_LOGI(TAG, "Sent %d bytes to client %lu", bytes_sent, (unsigned long)client->client_id);
    
    return ESP_OK;
}

/* Cleanup Client */
static void cleanup_client(mcp_tcp_transport_t *transport, mcp_tcp_client_t *client)
{
    if (client->socket >= 0) {
        close(client->socket);
        client->socket = -1;
    }
    
    client->connected = false;
    client->client_id = 0;
    
    if (transport->client_count > 0) {
        transport->client_count--;
        transport->stats.active_connections = transport->client_count;
    }
    
    ESP_LOGI(TAG, "Client cleaned up, active clients: %d", transport->client_count);
}

/* Find Free Client Slot */
static int find_free_client_slot(mcp_tcp_transport_t *transport)
{
    for (int i = 0; i < transport->config.max_clients; i++) {
        if (!transport->clients[i].connected) {
            return i;
        }
    }
    return -1;
}