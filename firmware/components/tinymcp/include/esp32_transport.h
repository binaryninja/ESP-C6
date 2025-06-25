/**
 * @file esp32_transport.h
 * @brief ESP32-C6 MCP Transport Layer Interface
 * 
 * This header provides the transport layer abstraction for MCP communication
 * on ESP32-C6, supporting UART/USB CDC, WiFi TCP, and BLE transports.
 * 
 * Features:
 * - Multiple transport backends (UART, TCP, BLE)
 * - JSON-RPC message framing
 * - Asynchronous message handling
 * - Flow control and buffering
 * - Connection management
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"

/* Transport Configuration */
#define MCP_TRANSPORT_MAX_CONNECTIONS   4
#define MCP_TRANSPORT_RX_BUFFER_SIZE    2048
#define MCP_TRANSPORT_TX_BUFFER_SIZE    2048
#define MCP_TRANSPORT_QUEUE_SIZE        16
#define MCP_TRANSPORT_TIMEOUT_MS        5000

/* UART Transport Configuration */
#define MCP_UART_NUM                    UART_NUM_0
#define MCP_UART_BAUD_RATE             115200
#define MCP_UART_DATA_BITS             UART_DATA_8_BITS
#define MCP_UART_PARITY                UART_PARITY_DISABLE
#define MCP_UART_STOP_BITS             UART_STOP_BITS_1
#define MCP_UART_FLOW_CTRL             UART_HW_FLOWCTRL_DISABLE

/* Message Framing */
#define MCP_MESSAGE_START_MARKER       0x7E
#define MCP_MESSAGE_END_MARKER         0x7F
#define MCP_MESSAGE_ESCAPE_CHAR        0x7D
#define MCP_MESSAGE_ESCAPE_XOR         0x20

/* Transport Handle */
typedef struct esp32_mcp_transport* esp32_mcp_transport_handle_t;

/* Transport Types */
typedef enum {
    MCP_TRANSPORT_TYPE_UART = 0,
    MCP_TRANSPORT_TYPE_USB_CDC,
    MCP_TRANSPORT_TYPE_WIFI_TCP,
    MCP_TRANSPORT_TYPE_BLE,
    MCP_TRANSPORT_TYPE_MAX
} mcp_transport_type_t;

/* Transport States */
typedef enum {
    MCP_TRANSPORT_STATE_DISCONNECTED = 0,
    MCP_TRANSPORT_STATE_CONNECTING,
    MCP_TRANSPORT_STATE_CONNECTED,
    MCP_TRANSPORT_STATE_DISCONNECTING,
    MCP_TRANSPORT_STATE_ERROR
} mcp_transport_state_t;

/* Transport Events */
typedef enum {
    MCP_TRANSPORT_EVENT_CONNECTED = 0,
    MCP_TRANSPORT_EVENT_DISCONNECTED,
    MCP_TRANSPORT_EVENT_DATA_RECEIVED,
    MCP_TRANSPORT_EVENT_DATA_SENT,
    MCP_TRANSPORT_EVENT_ERROR
} mcp_transport_event_t;

/* UART Transport Configuration */
typedef struct {
    uart_port_t uart_num;
    int baud_rate;
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
    uart_hw_flowcontrol_t flow_ctrl;
    int tx_pin;
    int rx_pin;
    int rts_pin;
    int cts_pin;
} mcp_uart_config_t;

/* TCP Transport Configuration */
typedef struct {
    int port;
    const char* bind_addr;
    int max_connections;
    int keepalive_idle;
    int keepalive_interval;
    int keepalive_count;
} mcp_tcp_config_t;

/* BLE Transport Configuration */
typedef struct {
    const char* device_name;
    const char* service_uuid;
    const char* char_uuid;
    uint16_t mtu_size;
} mcp_ble_config_t;

/* Transport Configuration */
typedef struct {
    mcp_transport_type_t type;
    uint32_t rx_buffer_size;
    uint32_t tx_buffer_size;
    uint32_t queue_size;
    uint32_t timeout_ms;
    bool enable_framing;
    bool enable_flow_control;
    
    union {
        mcp_uart_config_t uart;
        mcp_tcp_config_t tcp;
        mcp_ble_config_t ble;
    } config;
    
} mcp_transport_config_t;

/* Transport Message */
typedef struct {
    uint8_t* data;
    size_t length;
    size_t capacity;
    int64_t timestamp;
    uint32_t connection_id;
} mcp_transport_message_t;

/* Transport Statistics */
typedef struct {
    uint32_t bytes_received;
    uint32_t bytes_sent;
    uint32_t messages_received;
    uint32_t messages_sent;
    uint32_t connection_count;
    uint32_t error_count;
    uint32_t buffer_overruns;
    uint32_t framing_errors;
} mcp_transport_stats_t;

/* Transport Event Callback */
typedef void (*mcp_transport_event_cb_t)(esp32_mcp_transport_handle_t transport,
                                        mcp_transport_event_t event,
                                        void* event_data,
                                        void* user_data);

/* Transport Message Callback */
typedef void (*mcp_transport_message_cb_t)(esp32_mcp_transport_handle_t transport,
                                          const mcp_transport_message_t* message,
                                          void* user_data);

/**
 * @brief Get default transport configuration
 * 
 * @param type Transport type
 * @param config Pointer to configuration structure to fill
 * @return ESP_OK on success
 */
esp_err_t esp32_mcp_transport_get_default_config(mcp_transport_type_t type,
                                                 mcp_transport_config_t* config);

/**
 * @brief Initialize transport layer
 * 
 * @param config Transport configuration
 * @param transport_handle Pointer to store transport handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_transport_init(const mcp_transport_config_t* config,
                                   esp32_mcp_transport_handle_t* transport_handle);

/**
 * @brief Start transport layer
 * 
 * @param transport_handle Transport handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_transport_start(esp32_mcp_transport_handle_t transport_handle);

/**
 * @brief Stop transport layer
 * 
 * @param transport_handle Transport handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_transport_stop(esp32_mcp_transport_handle_t transport_handle);

/**
 * @brief Deinitialize transport layer
 * 
 * @param transport_handle Transport handle
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_transport_deinit(esp32_mcp_transport_handle_t transport_handle);

/**
 * @brief Set event callback
 * 
 * @param transport_handle Transport handle
 * @param callback Event callback function
 * @param user_data User data passed to callback
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_transport_set_event_callback(esp32_mcp_transport_handle_t transport_handle,
                                                 mcp_transport_event_cb_t callback,
                                                 void* user_data);

/**
 * @brief Set message callback
 * 
 * @param transport_handle Transport handle
 * @param callback Message callback function
 * @param user_data User data passed to callback
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_transport_set_message_callback(esp32_mcp_transport_handle_t transport_handle,
                                                   mcp_transport_message_cb_t callback,
                                                   void* user_data);

/**
 * @brief Send message
 * 
 * @param transport_handle Transport handle
 * @param message Message to send
 * @param connection_id Connection ID (0 for single connection transports)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_transport_send_message(esp32_mcp_transport_handle_t transport_handle,
                                           const mcp_transport_message_t* message,
                                           uint32_t connection_id);

/**
 * @brief Send data directly
 * 
 * @param transport_handle Transport handle
 * @param data Data buffer
 * @param length Data length
 * @param connection_id Connection ID (0 for single connection transports)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_transport_send_data(esp32_mcp_transport_handle_t transport_handle,
                                        const uint8_t* data,
                                        size_t length,
                                        uint32_t connection_id);

/**
 * @brief Get transport state
 * 
 * @param transport_handle Transport handle
 * @return Current transport state
 */
mcp_transport_state_t esp32_mcp_transport_get_state(esp32_mcp_transport_handle_t transport_handle);

/**
 * @brief Get transport statistics
 * 
 * @param transport_handle Transport handle
 * @param stats Pointer to store statistics
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_transport_get_stats(esp32_mcp_transport_handle_t transport_handle,
                                        mcp_transport_stats_t* stats);

/**
 * @brief Check if transport is connected
 * 
 * @param transport_handle Transport handle
 * @return true if connected, false otherwise
 */
bool esp32_mcp_transport_is_connected(esp32_mcp_transport_handle_t transport_handle);

/**
 * @brief Get connection count
 * 
 * @param transport_handle Transport handle
 * @return Number of active connections
 */
uint32_t esp32_mcp_transport_get_connection_count(esp32_mcp_transport_handle_t transport_handle);

/**
 * @brief Allocate transport message
 * 
 * @param capacity Message buffer capacity
 * @return Allocated message or NULL on failure
 */
mcp_transport_message_t* esp32_mcp_transport_alloc_message(size_t capacity);

/**
 * @brief Free transport message
 * 
 * @param message Message to free
 */
void esp32_mcp_transport_free_message(mcp_transport_message_t* message);

/**
 * @brief Frame message for transmission
 * 
 * Adds start/end markers and escapes special characters
 * 
 * @param input Input data
 * @param input_len Input data length
 * @param output Output buffer
 * @param output_size Output buffer size
 * @param output_len Actual output length
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_transport_frame_message(const uint8_t* input,
                                            size_t input_len,
                                            uint8_t* output,
                                            size_t output_size,
                                            size_t* output_len);

/**
 * @brief Unframe received message
 * 
 * Removes start/end markers and unescapes special characters
 * 
 * @param input Input framed data
 * @param input_len Input data length
 * @param output Output buffer
 * @param output_size Output buffer size
 * @param output_len Actual output length
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t esp32_mcp_transport_unframe_message(const uint8_t* input,
                                              size_t input_len,
                                              uint8_t* output,
                                              size_t output_size,
                                              size_t* output_len);

#ifdef __cplusplus
}
#endif