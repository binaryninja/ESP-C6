/**
 * @file esp32_transport.cpp
 * @brief ESP32-C6 MCP Transport Layer Implementation
 * 
 * This file implements the transport layer for MCP communication on ESP32-C6,
 * supporting UART/USB CDC with message framing and flow control.
 */

#include "esp32_transport.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"

static const char *TAG = "MCP_TRANSPORT";

/* Transport Internal Structure */
typedef struct esp32_mcp_transport {
    /* Configuration */
    mcp_transport_config_t config;
    
    /* State */
    bool initialized;
    bool running;
    mcp_transport_state_t state;
    
    /* UART specific */
    uart_port_t uart_port;
    QueueHandle_t uart_queue;
    
    /* Tasks */
    TaskHandle_t rx_task;
    TaskHandle_t tx_task;
    
    /* Synchronization */
    SemaphoreHandle_t mutex;
    QueueHandle_t tx_queue;
    
    /* Buffers */
    uint8_t* rx_buffer;
    uint8_t* tx_buffer;
    uint8_t* frame_buffer;
    
    /* Message framing */
    uint8_t* partial_message;
    size_t partial_length;
    bool in_frame;
    bool escape_next;
    
    /* Callbacks */
    mcp_transport_event_cb_t event_callback;
    mcp_transport_message_cb_t message_callback;
    void* callback_user_data;
    
    /* Statistics */
    mcp_transport_stats_t stats;
    
} esp32_mcp_transport_t;

/* Forward declarations */
static void mcp_transport_rx_task(void* arg);
static void mcp_transport_tx_task(void* arg);
static esp_err_t mcp_transport_uart_init(esp32_mcp_transport_t* transport);
static esp_err_t mcp_transport_uart_deinit(esp32_mcp_transport_t* transport);
static esp_err_t mcp_process_received_data(esp32_mcp_transport_t* transport, 
                                          const uint8_t* data, size_t length);
static void mcp_transport_set_state(esp32_mcp_transport_t* transport, 
                                   mcp_transport_state_t new_state);
static void mcp_transport_send_event(esp32_mcp_transport_t* transport, 
                                    mcp_transport_event_t event, void* data);

/* Get default transport configuration */
esp_err_t esp32_mcp_transport_get_default_config(mcp_transport_type_t type,
                                                 mcp_transport_config_t* config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(config, 0, sizeof(mcp_transport_config_t));
    
    config->type = type;
    config->rx_buffer_size = MCP_TRANSPORT_RX_BUFFER_SIZE;
    config->tx_buffer_size = MCP_TRANSPORT_TX_BUFFER_SIZE;
    config->queue_size = MCP_TRANSPORT_QUEUE_SIZE;
    config->timeout_ms = MCP_TRANSPORT_TIMEOUT_MS;
    config->enable_framing = true;
    config->enable_flow_control = false;
    
    switch (type) {
        case MCP_TRANSPORT_TYPE_UART:
        case MCP_TRANSPORT_TYPE_USB_CDC:
            config->config.uart.uart_num = MCP_UART_NUM;
            config->config.uart.baud_rate = MCP_UART_BAUD_RATE;
            config->config.uart.data_bits = MCP_UART_DATA_BITS;
            config->config.uart.parity = MCP_UART_PARITY;
            config->config.uart.stop_bits = MCP_UART_STOP_BITS;
            config->config.uart.flow_ctrl = MCP_UART_FLOW_CTRL;
            config->config.uart.tx_pin = UART_PIN_NO_CHANGE;
            config->config.uart.rx_pin = UART_PIN_NO_CHANGE;
            config->config.uart.rts_pin = UART_PIN_NO_CHANGE;
            config->config.uart.cts_pin = UART_PIN_NO_CHANGE;
            break;
            
        case MCP_TRANSPORT_TYPE_WIFI_TCP:
            config->config.tcp.port = 80;
            config->config.tcp.bind_addr = "0.0.0.0";
            config->config.tcp.max_connections = 4;
            config->config.tcp.keepalive_idle = 7200;
            config->config.tcp.keepalive_interval = 75;
            config->config.tcp.keepalive_count = 9;
            break;
            
        case MCP_TRANSPORT_TYPE_BLE:
            config->config.ble.device_name = "ESP32-C6-MCP";
            config->config.ble.service_uuid = "12345678-1234-5678-9abc-123456789abc";
            config->config.ble.char_uuid = "87654321-4321-8765-cba9-987654321cba";
            config->config.ble.mtu_size = 512;
            break;
            
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    return ESP_OK;
}

/* Initialize transport layer */
esp_err_t esp32_mcp_transport_init(const mcp_transport_config_t* config,
                                   esp32_mcp_transport_handle_t* transport_handle)
{
    if (!config || !transport_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing MCP transport (type: %d)", config->type);
    
    /* Allocate transport structure */
    esp32_mcp_transport_t* transport = (esp32_mcp_transport_t*)heap_caps_calloc(1, 
                                        sizeof(esp32_mcp_transport_t), MALLOC_CAP_DEFAULT);
    if (!transport) {
        ESP_LOGE(TAG, "Failed to allocate transport structure");
        return ESP_ERR_NO_MEM;
    }
    
    /* Copy configuration */
    memcpy(&transport->config, config, sizeof(mcp_transport_config_t));
    
    /* Create synchronization objects */
    transport->mutex = xSemaphoreCreateMutex();
    if (!transport->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(transport);
        return ESP_ERR_NO_MEM;
    }
    
    transport->tx_queue = xQueueCreate(config->queue_size, sizeof(mcp_transport_message_t*));
    if (!transport->tx_queue) {
        ESP_LOGE(TAG, "Failed to create TX queue");
        vSemaphoreDelete(transport->mutex);
        free(transport);
        return ESP_ERR_NO_MEM;
    }
    
    /* Allocate buffers */
    transport->rx_buffer = (uint8_t*)heap_caps_malloc(config->rx_buffer_size, MALLOC_CAP_DEFAULT);
    transport->tx_buffer = (uint8_t*)heap_caps_malloc(config->tx_buffer_size, MALLOC_CAP_DEFAULT);
    transport->frame_buffer = (uint8_t*)heap_caps_malloc(config->tx_buffer_size * 2, MALLOC_CAP_DEFAULT);
    transport->partial_message = (uint8_t*)heap_caps_malloc(config->rx_buffer_size, MALLOC_CAP_DEFAULT);
    
    if (!transport->rx_buffer || !transport->tx_buffer || 
        !transport->frame_buffer || !transport->partial_message) {
        ESP_LOGE(TAG, "Failed to allocate buffers");
        goto cleanup;
    }
    
    /* Initialize transport-specific components */
    esp_err_t ret = ESP_OK;
    switch (config->type) {
        case MCP_TRANSPORT_TYPE_UART:
        case MCP_TRANSPORT_TYPE_USB_CDC:
            ret = mcp_transport_uart_init(transport);
            break;
            
        case MCP_TRANSPORT_TYPE_WIFI_TCP:
            ESP_LOGE(TAG, "TCP transport not implemented yet");
            ret = ESP_ERR_NOT_SUPPORTED;
            break;
            
        case MCP_TRANSPORT_TYPE_BLE:
            ESP_LOGE(TAG, "BLE transport not implemented yet");
            ret = ESP_ERR_NOT_SUPPORTED;
            break;
            
        default:
            ret = ESP_ERR_NOT_SUPPORTED;
            break;
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize transport: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    /* Initialize state */
    transport->state = MCP_TRANSPORT_STATE_DISCONNECTED;
    transport->initialized = true;
    memset(&transport->stats, 0, sizeof(mcp_transport_stats_t));
    
    *transport_handle = transport;
    ESP_LOGI(TAG, "MCP transport initialized successfully");
    return ESP_OK;
    
cleanup:
    if (transport->partial_message) free(transport->partial_message);
    if (transport->frame_buffer) free(transport->frame_buffer);
    if (transport->tx_buffer) free(transport->tx_buffer);
    if (transport->rx_buffer) free(transport->rx_buffer);
    if (transport->tx_queue) vQueueDelete(transport->tx_queue);
    if (transport->mutex) vSemaphoreDelete(transport->mutex);
    free(transport);
    return ret;
}

/* Start transport layer */
esp_err_t esp32_mcp_transport_start(esp32_mcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_mcp_transport_t* transport = (esp32_mcp_transport_t*)transport_handle;
    
    if (!transport->initialized) {
        ESP_LOGE(TAG, "Transport not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (transport->running) {
        ESP_LOGW(TAG, "Transport already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting MCP transport");
    
    mcp_transport_set_state(transport, MCP_TRANSPORT_STATE_CONNECTING);
    
    /* Create RX task */
    BaseType_t task_ret = xTaskCreate(mcp_transport_rx_task, 
                                     "mcp_transport_rx", 
                                     4096,
                                     transport, 
                                     5, 
                                     &transport->rx_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RX task");
        return ESP_ERR_NO_MEM;
    }
    
    /* Create TX task */
    task_ret = xTaskCreate(mcp_transport_tx_task, 
                          "mcp_transport_tx", 
                          4096,
                          transport, 
                          5, 
                          &transport->tx_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TX task");
        vTaskDelete(transport->rx_task);
        return ESP_ERR_NO_MEM;
    }
    
    transport->running = true;
    mcp_transport_set_state(transport, MCP_TRANSPORT_STATE_CONNECTED);
    
    ESP_LOGI(TAG, "MCP transport started successfully");
    return ESP_OK;
}

/* Stop transport layer */
esp_err_t esp32_mcp_transport_stop(esp32_mcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_mcp_transport_t* transport = (esp32_mcp_transport_t*)transport_handle;
    
    if (!transport->running) {
        ESP_LOGW(TAG, "Transport not running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping MCP transport");
    
    mcp_transport_set_state(transport, MCP_TRANSPORT_STATE_DISCONNECTING);
    transport->running = false;
    
    /* Delete tasks */
    if (transport->rx_task) {
        vTaskDelete(transport->rx_task);
        transport->rx_task = NULL;
    }
    
    if (transport->tx_task) {
        vTaskDelete(transport->tx_task);
        transport->tx_task = NULL;
    }
    
    mcp_transport_set_state(transport, MCP_TRANSPORT_STATE_DISCONNECTED);
    ESP_LOGI(TAG, "MCP transport stopped");
    return ESP_OK;
}

/* Deinitialize transport layer */
esp_err_t esp32_mcp_transport_deinit(esp32_mcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_mcp_transport_t* transport = (esp32_mcp_transport_t*)transport_handle;
    
    /* Stop transport if running */
    if (transport->running) {
        esp32_mcp_transport_stop(transport_handle);
    }
    
    ESP_LOGI(TAG, "Deinitializing MCP transport");
    
    /* Cleanup transport-specific components */
    switch (transport->config.type) {
        case MCP_TRANSPORT_TYPE_UART:
        case MCP_TRANSPORT_TYPE_USB_CDC:
            mcp_transport_uart_deinit(transport);
            break;
        default:
            break;
    }
    
    /* Free buffers */
    if (transport->partial_message) free(transport->partial_message);
    if (transport->frame_buffer) free(transport->frame_buffer);
    if (transport->tx_buffer) free(transport->tx_buffer);
    if (transport->rx_buffer) free(transport->rx_buffer);
    
    /* Cleanup synchronization objects */
    if (transport->tx_queue) {
        vQueueDelete(transport->tx_queue);
    }
    if (transport->mutex) {
        vSemaphoreDelete(transport->mutex);
    }
    
    free(transport);
    ESP_LOGI(TAG, "MCP transport deinitialized");
    return ESP_OK;
}

/* UART transport initialization */
static esp_err_t mcp_transport_uart_init(esp32_mcp_transport_t* transport)
{
    const mcp_uart_config_t* uart_config = &transport->config.config.uart;
    
    uart_config_t config = {
        .baud_rate = uart_config->baud_rate,
        .data_bits = uart_config->data_bits,
        .parity = uart_config->parity,
        .stop_bits = uart_config->stop_bits,
        .flow_ctrl = uart_config->flow_ctrl,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t ret = uart_param_config(uart_config->uart_num, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_set_pin(uart_config->uart_num, 
                       uart_config->tx_pin, 
                       uart_config->rx_pin,
                       uart_config->rts_pin, 
                       uart_config->cts_pin);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_driver_install(uart_config->uart_num, 
                             transport->config.rx_buffer_size * 2,
                             transport->config.tx_buffer_size * 2, 
                             transport->config.queue_size,
                             &transport->uart_queue, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    transport->uart_port = uart_config->uart_num;
    ESP_LOGI(TAG, "UART transport initialized (port: %d, baud: %d)", 
             uart_config->uart_num, uart_config->baud_rate);
    
    return ESP_OK;
}

/* UART transport deinitialization */
static esp_err_t mcp_transport_uart_deinit(esp32_mcp_transport_t* transport)
{
    uart_driver_delete(transport->uart_port);
    return ESP_OK;
}

/* RX task */
static void mcp_transport_rx_task(void* arg)
{
    esp32_mcp_transport_t* transport = (esp32_mcp_transport_t*)arg;
    uart_event_t event;
    
    ESP_LOGI(TAG, "MCP transport RX task started");
    
    while (transport->running) {
        if (xQueueReceive(transport->uart_queue, &event, pdMS_TO_TICKS(1000)) == pdTRUE) {
            switch (event.type) {
                case UART_DATA:
                    if (event.size > 0) {
                        int length = uart_read_bytes(transport->uart_port, 
                                                   transport->rx_buffer, 
                                                   event.size, 
                                                   pdMS_TO_TICKS(100));
                        if (length > 0) {
                            transport->stats.bytes_received += length;
                            mcp_process_received_data(transport, transport->rx_buffer, length);
                        }
                    }
                    break;
                    
                case UART_FIFO_OVF:
                    ESP_LOGW(TAG, "UART FIFO overflow");
                    uart_flush_input(transport->uart_port);
                    transport->stats.buffer_overruns++;
                    break;
                    
                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "UART buffer full");
                    uart_flush_input(transport->uart_port);
                    transport->stats.buffer_overruns++;
                    break;
                    
                case UART_BREAK:
                case UART_PARITY_ERR:
                case UART_FRAME_ERR:
                    ESP_LOGW(TAG, "UART error event: %d", event.type);
                    transport->stats.error_count++;
                    break;
                    
                default:
                    break;
            }
        }
    }
    
    ESP_LOGI(TAG, "MCP transport RX task stopped");
    vTaskDelete(NULL);
}

/* TX task */
static void mcp_transport_tx_task(void* arg)
{
    esp32_mcp_transport_t* transport = (esp32_mcp_transport_t*)arg;
    mcp_transport_message_t* message;
    
    ESP_LOGI(TAG, "MCP transport TX task started");
    
    while (transport->running) {
        if (xQueueReceive(transport->tx_queue, &message, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (message && message->data && message->length > 0) {
                size_t bytes_to_send = message->length;
                const uint8_t* data_to_send = message->data;
                
                /* Frame message if enabled */
                if (transport->config.enable_framing) {
                    size_t framed_length;
                    esp_err_t ret = esp32_mcp_transport_frame_message(message->data, 
                                                                    message->length,
                                                                    transport->frame_buffer,
                                                                    transport->config.tx_buffer_size * 2,
                                                                    &framed_length);
                    if (ret == ESP_OK) {
                        data_to_send = transport->frame_buffer;
                        bytes_to_send = framed_length;
                    } else {
                        ESP_LOGW(TAG, "Failed to frame message, sending raw");
                    }
                }
                
                /* Send data */
                int sent = uart_write_bytes(transport->uart_port, data_to_send, bytes_to_send);
                if (sent > 0) {
                    transport->stats.bytes_sent += sent;
                    transport->stats.messages_sent++;
                    mcp_transport_send_event(transport, MCP_TRANSPORT_EVENT_DATA_SENT, message);
                } else {
                    ESP_LOGW(TAG, "Failed to send message");
                    transport->stats.error_count++;
                }
            }
            
            /* Free message */
            esp32_mcp_transport_free_message(message);
        }
    }
    
    ESP_LOGI(TAG, "MCP transport TX task stopped");
    vTaskDelete(NULL);
}

/* Process received data */
static esp_err_t mcp_process_received_data(esp32_mcp_transport_t* transport, 
                                          const uint8_t* data, size_t length)
{
    if (!transport->config.enable_framing) {
        /* No framing - treat as complete message */
        mcp_transport_message_t* message = esp32_mcp_transport_alloc_message(length);
        if (message) {
            memcpy(message->data, data, length);
            message->length = length;
            message->timestamp = esp_timer_get_time();
            message->connection_id = 0;
            
            transport->stats.messages_received++;
            
            if (transport->message_callback) {
                transport->message_callback(transport, message, transport->callback_user_data);
            } else {
                esp32_mcp_transport_free_message(message);
            }
        }
        return ESP_OK;
    }
    
    /* Process framed data */
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        
        if (transport->escape_next) {
            /* Previous byte was escape character */
            transport->escape_next = false;
            byte ^= MCP_MESSAGE_ESCAPE_XOR;
            
            if (transport->partial_length < transport->config.rx_buffer_size) {
                transport->partial_message[transport->partial_length++] = byte;
            } else {
                /* Buffer overflow */
                transport->stats.buffer_overruns++;
                transport->partial_length = 0;
                transport->in_frame = false;
            }
            
        } else if (byte == MCP_MESSAGE_START_MARKER) {
            /* Start of new frame */
            transport->in_frame = true;
            transport->partial_length = 0;
            
        } else if (byte == MCP_MESSAGE_END_MARKER) {
            /* End of frame */
            if (transport->in_frame && transport->partial_length > 0) {
                /* Complete message received */
                mcp_transport_message_t* message = esp32_mcp_transport_alloc_message(transport->partial_length);
                if (message) {
                    memcpy(message->data, transport->partial_message, transport->partial_length);
                    message->length = transport->partial_length;
                    message->timestamp = esp_timer_get_time();
                    message->connection_id = 0;
                    
                    transport->stats.messages_received++;
                    
                    if (transport->message_callback) {
                        transport->message_callback(transport, message, transport->callback_user_data);
                    } else {
                        esp32_mcp_transport_free_message(message);
                    }
                }
            }
            transport->in_frame = false;
            transport->partial_length = 0;
            
        } else if (byte == MCP_MESSAGE_ESCAPE_CHAR) {
            /* Escape character - next byte is escaped */
            transport->escape_next = true;
            
        } else if (transport->in_frame) {
            /* Regular data byte */
            if (transport->partial_length < transport->config.rx_buffer_size) {
                transport->partial_message[transport->partial_length++] = byte;
            } else {
                /* Buffer overflow */
                transport->stats.buffer_overruns++;
                transport->partial_length = 0;
                transport->in_frame = false;
            }
        }
        /* Ignore bytes outside of frames */
    }
    
    return ESP_OK;
}

/* Set transport state */
static void mcp_transport_set_state(esp32_mcp_transport_t* transport, 
                                   mcp_transport_state_t new_state)
{
    if (transport->state != new_state) {
        transport->state = new_state;
        mcp_transport_send_event(transport, 
                                new_state == MCP_TRANSPORT_STATE_CONNECTED ? 
                                MCP_TRANSPORT_EVENT_CONNECTED : MCP_TRANSPORT_EVENT_DISCONNECTED, 
                                NULL);
    }
}

/* Send event to callback */
static void mcp_transport_send_event(esp32_mcp_transport_t* transport, 
                                    mcp_transport_event_t event, void* data)
{
    if (transport->event_callback) {
        transport->event_callback(transport, event, data, transport->callback_user_data);
    }
}

/* Public API implementations */

esp_err_t esp32_mcp_transport_set_message_callback(esp32_mcp_transport_handle_t transport_handle,
                                                   mcp_transport_message_cb_t callback,
                                                   void* user_data)
{
    if (!transport_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_mcp_transport_t* transport = (esp32_mcp_transport_t*)transport_handle;
    transport->message_callback = callback;
    transport->callback_user_data = user_data;
    
    return ESP_OK;
}

esp_err_t esp32_mcp_transport_set_event_callback(esp32_mcp_transport_handle_t transport_handle,
                                                 mcp_transport_event_cb_t callback,
                                                 void* user_data)
{
    if (!transport_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_mcp_transport_t* transport = (esp32_mcp_transport_t*)transport_handle;
    transport->event_callback = callback;
    transport->callback_user_data = user_data;
    
    return ESP_OK;
}

esp_err_t esp32_mcp_transport_send_data(esp32_mcp_transport_handle_t transport_handle,
                                        const uint8_t* data,
                                        size_t length,
                                        uint32_t connection_id)
{
    if (!transport_handle || !data || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_mcp_transport_t* transport = (esp32_mcp_transport_t*)transport_handle;
    
    if (!transport->running) {
        return ESP_ERR_INVALID_STATE;
    }
    
    mcp_transport_message_t* message = esp32_mcp_transport_alloc_message(length);
    if (!message) {
        return ESP_ERR_NO_MEM;
    }
    
    memcpy(message->data, data, length);
    message->length = length;
    message->connection_id = connection_id;
    message->timestamp = esp_timer_get_time();
    
    if (xQueueSend(transport->tx_queue, &message, pdMS_TO_TICKS(1000)) != pdTRUE) {
        esp32_mcp_transport_free_message(message);
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

bool esp32_mcp_transport_is_connected(esp32_mcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return false;
    }
    
    esp32_mcp_transport_t* transport = (esp32_mcp_transport_t*)transport_handle;
    return transport->state == MCP_TRANSPORT_STATE_CONNECTED;
}

mcp_transport_message_t* esp32_mcp_transport_alloc_message(size_t capacity)
{
    mcp_transport_message_t* message = (mcp_transport_message_t*)heap_caps_malloc(
        sizeof(mcp_transport_message_t), MALLOC_CAP_DEFAULT);
    if (!message) {
        return NULL;
    }
    
    message->data = (uint8_t*)heap_caps_malloc(capacity, MALLOC_CAP_DEFAULT);
    if (!message->data) {
        free(message);
        return NULL;
    }
    
    message->capacity = capacity;
    message->length = 0;
    message->timestamp = 0;
    message->connection_id = 0;
    
    return message;
}

void esp32_mcp_transport_free_message(mcp_transport_message_t* message)
{
    if (message) {
        if (message->data) {
            free(message->data);
        }
        free(message);
    }
}

esp_err_t esp32_mcp_transport_frame_message(const uint8_t* input,
                                            size_t input_len,
                                            uint8_t* output,
                                            size_t output_size,
                                            size_t* output_len)
{
    if (!input || !output || !output_len || input_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t required_size = input_len * 2 + 2; // Worst case: all bytes escaped + start/end markers
    if (output_size < required_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    size_t pos = 0;
    
    /* Start marker */
    output[pos++] = MCP_MESSAGE_START_MARKER;
    
    /* Escape and copy data */
    for (size_t i = 0; i < input_len; i++) {
        uint8_t byte = input[i];
        
        if (byte == MCP_MESSAGE_START_MARKER || 
            byte == MCP_MESSAGE_END_MARKER || 
            byte == MCP_MESSAGE_ESCAPE_CHAR) {
            /* Escape special characters */
            output[pos++] = MCP_MESSAGE_ESCAPE_CHAR;
            output[pos++] = byte ^ MCP_MESSAGE_ESCAPE_XOR;
        } else {
            /* Regular byte */
            output[pos++] = byte;
        }
    }
    
    /* End marker */
    output[pos++] = MCP_MESSAGE_END_MARKER;
    
    *output_len = pos;
    return ESP_OK;
}

esp_err_t esp32_mcp_transport_unframe_message(const uint8_t* input,
                                              size_t input_len,
                                              uint8_t* output,
                                              size_t output_size,
                                              size_t* output_len)
{
    if (!input || !output || !output_len || input_len < 2) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Check for start and end markers */
    if (input[0] != MCP_MESSAGE_START_MARKER || 
        input[input_len - 1] != MCP_MESSAGE_END_MARKER) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t pos = 0;
    bool escape_next = false;
    
    /* Process bytes between markers */
    for (size_t i = 1; i < input_len - 1; i++) {
        uint8_t byte = input[i];
        
        if (escape_next) {
            /* Previous byte was escape character */
            escape_next = false;
            byte ^= MCP_MESSAGE_ESCAPE_XOR;
            
            if (pos >= output_size) {
                return ESP_ERR_INVALID_SIZE;
            }
            output[pos++] = byte;
            
        } else if (byte == MCP_MESSAGE_ESCAPE_CHAR) {
            /* Escape character - next byte is escaped */
            escape_next = true;
            
        } else {
            /* Regular byte */
            if (pos >= output_size) {
                return ESP_ERR_INVALID_SIZE;
            }
            output[pos++] = byte;
        }
    }
    
    *output_len = pos;
    return ESP_OK;
}

esp_err_t esp32_mcp_transport_get_stats(esp32_mcp_transport_handle_t transport_handle,
                                        mcp_transport_stats_t* stats)
{
    if (!transport_handle || !stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_mcp_transport_t* transport = (esp32_mcp_transport_t*)transport_handle;
    
    if (xSemaphoreTake(transport->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &transport->stats, sizeof(mcp_transport_stats_t));
        xSemaphoreGive(transport->mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

mcp_transport_state_t esp32_mcp_transport_get_state(esp32_mcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return MCP_TRANSPORT_STATE_ERROR;
    }
    
    esp32_mcp_transport_t* transport = (esp32_mcp_transport_t*)transport_handle;
    return transport->state;
}

uint32_t esp32_mcp_transport_get_connection_count(esp32_mcp_transport_handle_t transport_handle)
{
    if (!transport_handle) {
        return 0;
    }
    
    esp32_mcp_transport_t* transport = (esp32_mcp_transport_t*)transport_handle;
    return transport->running ? 1 : 0; // Single connection for UART
}