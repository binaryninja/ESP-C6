/**
 * @file mcp_tcp_transport.h
 * @brief ESP32-C6 MCP TCP Transport Layer
 * 
 * This component provides TCP transport for the Model Context Protocol (MCP) server
 * running on ESP32-C6. It creates a TCP server on port 8080 when WiFi is connected
 * and handles JSON-RPC communication with MCP clients.
 */

#ifndef MCP_TCP_TRANSPORT_H
#define MCP_TCP_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MCP TCP Transport Configuration
 */
typedef struct {
    uint16_t server_port;               ///< TCP server port (default: 8080)
    uint8_t max_clients;                ///< Maximum concurrent clients
    uint32_t buffer_size;               ///< Buffer size for messages
    uint32_t task_stack_size;           ///< Task stack size
    uint8_t task_priority;              ///< Task priority
    uint32_t keep_alive_idle;           ///< Keep-alive idle time (seconds)
    uint32_t keep_alive_interval;       ///< Keep-alive interval (seconds)
    uint32_t keep_alive_count;          ///< Keep-alive probe count
} mcp_tcp_transport_config_t;

/**
 * @brief MCP TCP Transport Status
 */
typedef enum {
    MCP_TCP_STATUS_STOPPED = 0,         ///< Transport stopped
    MCP_TCP_STATUS_STARTING,            ///< Transport starting
    MCP_TCP_STATUS_LISTENING,           ///< Server listening for connections
    MCP_TCP_STATUS_ERROR                ///< Transport error
} mcp_tcp_transport_status_t;

/**
 * @brief MCP TCP Transport Statistics
 */
typedef struct {
    uint32_t total_connections;         ///< Total connections received
    uint32_t active_connections;        ///< Currently active connections
    uint32_t messages_received;         ///< Total messages received
    uint32_t messages_sent;             ///< Total messages sent
    uint32_t bytes_received;            ///< Total bytes received
    uint32_t bytes_sent;                ///< Total bytes sent
    uint32_t errors;                    ///< Total errors
    uint64_t uptime_ms;                 ///< Transport uptime in milliseconds
} mcp_tcp_transport_stats_t;

/**
 * @brief MCP TCP Transport Handle
 */
typedef void* mcp_tcp_transport_handle_t;

/**
 * @brief Default MCP TCP Transport Configuration
 */
#define MCP_TCP_TRANSPORT_CONFIG_DEFAULT() { \
    .server_port = 8080, \
    .max_clients = 4, \
    .buffer_size = 2048, \
    .task_stack_size = 8192, \
    .task_priority = 6, \
    .keep_alive_idle = 7200, \
    .keep_alive_interval = 75, \
    .keep_alive_count = 9 \
}

/**
 * @brief Initialize MCP TCP Transport
 * 
 * Initializes the TCP transport layer for MCP communication.
 * This function sets up the transport but does not start the server.
 * 
 * @param config Transport configuration
 * @param transport_handle Pointer to store transport handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tcp_transport_init(const mcp_tcp_transport_config_t *config, 
                                 mcp_tcp_transport_handle_t *transport_handle);

/**
 * @brief Start MCP TCP Transport
 * 
 * Starts the TCP server and begins listening for client connections.
 * This should be called when WiFi is connected.
 * 
 * @param transport_handle Transport handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tcp_transport_start(mcp_tcp_transport_handle_t transport_handle);

/**
 * @brief Stop MCP TCP Transport
 * 
 * Stops the TCP server and closes all client connections.
 * This should be called when WiFi is disconnected.
 * 
 * @param transport_handle Transport handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tcp_transport_stop(mcp_tcp_transport_handle_t transport_handle);

/**
 * @brief Deinitialize MCP TCP Transport
 * 
 * Cleans up transport resources and frees memory.
 * 
 * @param transport_handle Transport handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tcp_transport_deinit(mcp_tcp_transport_handle_t transport_handle);

/**
 * @brief Get Transport Status
 * 
 * @param transport_handle Transport handle
 * @return Current transport status
 */
mcp_tcp_transport_status_t mcp_tcp_transport_get_status(mcp_tcp_transport_handle_t transport_handle);

/**
 * @brief Get Transport Statistics
 * 
 * @param transport_handle Transport handle
 * @param stats Pointer to statistics structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tcp_transport_get_stats(mcp_tcp_transport_handle_t transport_handle, 
                                      mcp_tcp_transport_stats_t *stats);

/**
 * @brief Check if Transport is Running
 * 
 * @param transport_handle Transport handle
 * @return true if transport is actively listening, false otherwise
 */
bool mcp_tcp_transport_is_running(mcp_tcp_transport_handle_t transport_handle);

/**
 * @brief Send Message to Client
 * 
 * Sends a JSON-RPC response message to a specific client.
 * This function is typically called by the MCP server.
 * 
 * @param transport_handle Transport handle
 * @param client_id Client identifier
 * @param message JSON message to send
 * @param message_len Message length
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tcp_transport_send_message(mcp_tcp_transport_handle_t transport_handle,
                                         uint32_t client_id,
                                         const char *message,
                                         size_t message_len);

/**
 * @brief Broadcast Message to All Clients
 * 
 * Sends a message to all connected clients.
 * 
 * @param transport_handle Transport handle
 * @param message JSON message to send
 * @param message_len Message length
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tcp_transport_broadcast_message(mcp_tcp_transport_handle_t transport_handle,
                                              const char *message,
                                              size_t message_len);

/**
 * @brief Reset Transport Statistics
 * 
 * Resets all counters and statistics to zero.
 * 
 * @param transport_handle Transport handle
 */
void mcp_tcp_transport_reset_stats(mcp_tcp_transport_handle_t transport_handle);

/**
 * @brief Set MCP Server Handle
 * 
 * Associates the transport with an MCP server for message processing.
 * 
 * @param transport_handle Transport handle
 * @param mcp_server_handle MCP server handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mcp_tcp_transport_set_mcp_server(mcp_tcp_transport_handle_t transport_handle,
                                           void *mcp_server_handle);

/**
 * @brief Get Server Port
 * 
 * @param transport_handle Transport handle
 * @return Server port number, 0 if not initialized
 */
uint16_t mcp_tcp_transport_get_port(mcp_tcp_transport_handle_t transport_handle);

/**
 * @brief Get Active Client Count
 * 
 * @param transport_handle Transport handle
 * @return Number of currently connected clients
 */
uint8_t mcp_tcp_transport_get_client_count(mcp_tcp_transport_handle_t transport_handle);

#ifdef __cplusplus
}
#endif

#endif // MCP_TCP_TRANSPORT_H