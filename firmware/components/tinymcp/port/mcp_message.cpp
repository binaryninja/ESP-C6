/**
 * @file mcp_message.cpp
 * @brief MCP Message Port Stub for ESP32-C6
 * 
 * This file provides basic stubs for the MCP message handling
 * ported to ESP32-C6 with ESP-IDF integration.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"

static const char *TAG = "MCP_MESSAGE";

/* MCP Message Implementation Stub */

namespace MCP {

/* Message types */
enum MessageType {
    MESSAGE_TYPE_REQUEST = 0,
    MESSAGE_TYPE_RESPONSE,
    MESSAGE_TYPE_NOTIFICATION,
    MESSAGE_TYPE_ERROR
};

/* Message priorities */
enum MessagePriority {
    MESSAGE_PRIORITY_LOW = 0,
    MESSAGE_PRIORITY_NORMAL,
    MESSAGE_PRIORITY_HIGH,
    MESSAGE_PRIORITY_CRITICAL
};

/* Message header structure */
struct MessageHeader {
    uint32_t message_id;
    MessageType type;
    MessagePriority priority;
    uint32_t timestamp;
    uint32_t content_length;
    uint32_t sequence_number;
    uint16_t checksum;
    uint8_t version;
    uint8_t flags;
};

/* Message content structure */
struct MessageContent {
    char* data;
    size_t length;
    size_t capacity;
    cJSON* json_object;
};

/* MCP Message class */
class CMCPMessage {
public:
    CMCPMessage() :
        m_header{},
        m_content{},
        m_allocated(false),
        m_parsed(false)
    {
        m_header.version = 1;
        m_header.timestamp = esp_timer_get_time() / 1000;
        m_header.message_id = GenerateMessageId();
    }
    
    CMCPMessage(const char* json_data, size_t length) : CMCPMessage() {
        SetContent(json_data, length);
    }
    
    virtual ~CMCPMessage() {
        Cleanup();
    }
    
    /* Message ID generation */
    static uint32_t GenerateMessageId() {
        static uint32_t s_message_counter = 0;
        return ++s_message_counter;
    }
    
    /* Content management */
    esp_err_t SetContent(const char* data, size_t length) {
        if (!data || length == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        
        Cleanup();
        
        m_content.capacity = length + 1;
        m_content.data = (char*)heap_caps_malloc(m_content.capacity, MALLOC_CAP_DEFAULT);
        if (!m_content.data) {
            ESP_LOGE(TAG, "Failed to allocate message content buffer");
            return ESP_ERR_NO_MEM;
        }
        
        memcpy(m_content.data, data, length);
        m_content.data[length] = '\0';
        m_content.length = length;
        m_allocated = true;
        
        m_header.content_length = length;
        m_header.checksum = CalculateChecksum(data, length);
        
        return ESP_OK;
    }
    
    const char* GetContent() const {
        return m_content.data;
    }
    
    size_t GetContentLength() const {
        return m_content.length;
    }
    
    /* JSON parsing */
    esp_err_t ParseJson() {
        if (!m_content.data || m_content.length == 0) {
            return ESP_ERR_INVALID_STATE;
        }
        
        if (m_content.json_object) {
            cJSON_Delete(m_content.json_object);
        }
        
        m_content.json_object = cJSON_Parse(m_content.data);
        if (!m_content.json_object) {
            ESP_LOGE(TAG, "Failed to parse JSON content");
            return ESP_ERR_INVALID_ARG;
        }
        
        m_parsed = true;
        
        /* Extract message type and ID from JSON */
        ExtractJsonInfo();
        
        ESP_LOGD(TAG, "JSON parsed successfully for message ID: %u", m_header.message_id);
        return ESP_OK;
    }
    
    cJSON* GetJsonObject() const {
        return m_content.json_object;
    }
    
    /* Message header access */
    void SetMessageId(uint32_t id) {
        m_header.message_id = id;
    }
    
    uint32_t GetMessageId() const {
        return m_header.message_id;
    }
    
    void SetType(MessageType type) {
        m_header.type = type;
    }
    
    MessageType GetType() const {
        return m_header.type;
    }
    
    void SetPriority(MessagePriority priority) {
        m_header.priority = priority;
    }
    
    MessagePriority GetPriority() const {
        return m_header.priority;
    }
    
    uint32_t GetTimestamp() const {
        return m_header.timestamp;
    }
    
    uint16_t GetChecksum() const {
        return m_header.checksum;
    }
    
    /* Message validation */
    bool IsValid() const {
        if (!m_content.data || m_content.length == 0) {
            return false;
        }
        
        uint16_t calculated_checksum = CalculateChecksum(m_content.data, m_content.length);
        return calculated_checksum == m_header.checksum;
    }
    
    bool IsParsed() const {
        return m_parsed && m_content.json_object != nullptr;
    }
    
    /* Message serialization */
    esp_err_t Serialize(char* buffer, size_t buffer_size, size_t* output_length) const {
        if (!buffer || buffer_size == 0 || !output_length) {
            return ESP_ERR_INVALID_ARG;
        }
        
        if (!m_content.data || m_content.length == 0) {
            return ESP_ERR_INVALID_STATE;
        }
        
        /* Simple serialization - just copy JSON content */
        if (buffer_size < m_content.length + 1) {
            return ESP_ERR_INVALID_SIZE;
        }
        
        memcpy(buffer, m_content.data, m_content.length);
        buffer[m_content.length] = '\0';
        *output_length = m_content.length;
        
        return ESP_OK;
    }
    
    /* Message utilities */
    const char* GetMethod() const {
        if (!IsParsed()) {
            return nullptr;
        }
        
        cJSON* method = cJSON_GetObjectItem(m_content.json_object, "method");
        return (method && cJSON_IsString(method)) ? cJSON_GetStringValue(method) : nullptr;
    }
    
    cJSON* GetParams() const {
        if (!IsParsed()) {
            return nullptr;
        }
        
        return cJSON_GetObjectItem(m_content.json_object, "params");
    }
    
    cJSON* GetResult() const {
        if (!IsParsed()) {
            return nullptr;
        }
        
        return cJSON_GetObjectItem(m_content.json_object, "result");
    }
    
    cJSON* GetError() const {
        if (!IsParsed()) {
            return nullptr;
        }
        
        return cJSON_GetObjectItem(m_content.json_object, "error");
    }
    
    bool IsRequest() const {
        return GetMethod() != nullptr;
    }
    
    bool IsResponse() const {
        return IsParsed() && (GetResult() != nullptr || GetError() != nullptr);
    }
    
    bool IsNotification() const {
        if (!IsParsed()) {
            return false;
        }
        
        cJSON* id = cJSON_GetObjectItem(m_content.json_object, "id");
        return GetMethod() != nullptr && id == nullptr;
    }
    
    /* Message creation helpers */
    static CMCPMessage* CreateRequest(const char* method, cJSON* params, uint32_t id = 0) {
        cJSON* json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "jsonrpc", "2.0");
        cJSON_AddStringToObject(json, "method", method);
        
        if (id > 0) {
            cJSON_AddNumberToObject(json, "id", id);
        }
        
        if (params) {
            cJSON_AddItemToObject(json, "params", cJSON_Duplicate(params, 1));
        }
        
        char* json_string = cJSON_Print(json);
        CMCPMessage* message = nullptr;
        
        if (json_string) {
            message = new CMCPMessage(json_string, strlen(json_string));
            if (message) {
                message->SetType(id > 0 ? MESSAGE_TYPE_REQUEST : MESSAGE_TYPE_NOTIFICATION);
                message->SetMessageId(id > 0 ? id : GenerateMessageId());
            }
            free(json_string);
        }
        
        cJSON_Delete(json);
        return message;
    }
    
    static CMCPMessage* CreateResponse(uint32_t id, cJSON* result) {
        cJSON* json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "jsonrpc", "2.0");
        cJSON_AddNumberToObject(json, "id", id);
        
        if (result) {
            cJSON_AddItemToObject(json, "result", cJSON_Duplicate(result, 1));
        } else {
            cJSON_AddNullToObject(json, "result");
        }
        
        char* json_string = cJSON_Print(json);
        CMCPMessage* message = nullptr;
        
        if (json_string) {
            message = new CMCPMessage(json_string, strlen(json_string));
            if (message) {
                message->SetType(MESSAGE_TYPE_RESPONSE);
                message->SetMessageId(id);
            }
            free(json_string);
        }
        
        cJSON_Delete(json);
        return message;
    }
    
    static CMCPMessage* CreateError(uint32_t id, int error_code, const char* error_message) {
        cJSON* json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "jsonrpc", "2.0");
        cJSON_AddNumberToObject(json, "id", id);
        
        cJSON* error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", error_code);
        cJSON_AddStringToObject(error, "message", error_message);
        cJSON_AddItemToObject(json, "error", error);
        
        char* json_string = cJSON_Print(json);
        CMCPMessage* message = nullptr;
        
        if (json_string) {
            message = new CMCPMessage(json_string, strlen(json_string));
            if (message) {
                message->SetType(MESSAGE_TYPE_ERROR);
                message->SetMessageId(id);
            }
            free(json_string);
        }
        
        cJSON_Delete(json);
        return message;
    }
    
private:
    MessageHeader m_header;
    MessageContent m_content;
    bool m_allocated;
    bool m_parsed;
    
    void ExtractJsonInfo() {
        if (!m_content.json_object) {
            return;
        }
        
        /* Extract ID */
        cJSON* id = cJSON_GetObjectItem(m_content.json_object, "id");
        if (id && cJSON_IsNumber(id)) {
            m_header.message_id = (uint32_t)cJSON_GetNumberValue(id);
        }
        
        /* Determine message type */
        cJSON* method = cJSON_GetObjectItem(m_content.json_object, "method");
        cJSON* result = cJSON_GetObjectItem(m_content.json_object, "result");
        cJSON* error = cJSON_GetObjectItem(m_content.json_object, "error");
        
        if (method) {
            m_header.type = id ? MESSAGE_TYPE_REQUEST : MESSAGE_TYPE_NOTIFICATION;
        } else if (result || error) {
            m_header.type = error ? MESSAGE_TYPE_ERROR : MESSAGE_TYPE_RESPONSE;
        }
    }
    
    static uint16_t CalculateChecksum(const char* data, size_t length) {
        uint16_t checksum = 0;
        for (size_t i = 0; i < length; i++) {
            checksum += (uint8_t)data[i];
        }
        return checksum;
    }
    
    void Cleanup() {
        if (m_allocated && m_content.data) {
            free(m_content.data);
            m_content.data = nullptr;
            m_allocated = false;
        }
        
        if (m_content.json_object) {
            cJSON_Delete(m_content.json_object);
            m_content.json_object = nullptr;
        }
        
        memset(&m_content, 0, sizeof(m_content));
        m_parsed = false;
    }
};

} /* namespace MCP */

/* C-compatible wrapper functions for ESP32 integration */
extern "C" {

typedef struct mcp_message_handle* mcp_message_handle_t;

mcp_message_handle_t mcp_message_create(const char* json_data, size_t length) {
    if (!json_data || length == 0) {
        return nullptr;
    }
    
    MCP::CMCPMessage* message = new MCP::CMCPMessage(json_data, length);
    if (!message) {
        ESP_LOGE(TAG, "Failed to create message");
        return nullptr;
    }
    
    if (message->ParseJson() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to parse JSON in message");
        delete message;
        return nullptr;
    }
    
    return (mcp_message_handle_t)message;
}

mcp_message_handle_t mcp_message_create_request(const char* method, const char* params_json, uint32_t id) {
    if (!method) {
        return nullptr;
    }
    
    cJSON* params = nullptr;
    if (params_json) {
        params = cJSON_Parse(params_json);
    }
    
    MCP::CMCPMessage* message = MCP::CMCPMessage::CreateRequest(method, params, id);
    
    if (params) {
        cJSON_Delete(params);
    }
    
    return (mcp_message_handle_t)message;
}

mcp_message_handle_t mcp_message_create_response(uint32_t id, const char* result_json) {
    cJSON* result = nullptr;
    if (result_json) {
        result = cJSON_Parse(result_json);
    }
    
    MCP::CMCPMessage* message = MCP::CMCPMessage::CreateResponse(id, result);
    
    if (result) {
        cJSON_Delete(result);
    }
    
    return (mcp_message_handle_t)message;
}

mcp_message_handle_t mcp_message_create_error(uint32_t id, int error_code, const char* error_message) {
    return (mcp_message_handle_t)MCP::CMCPMessage::CreateError(id, error_code, error_message);
}

void mcp_message_destroy(mcp_message_handle_t handle) {
    if (handle) {
        MCP::CMCPMessage* message = (MCP::CMCPMessage*)handle;
        delete message;
    }
}

const char* mcp_message_get_content(mcp_message_handle_t handle) {
    if (!handle) {
        return nullptr;
    }
    
    MCP::CMCPMessage* message = (MCP::CMCPMessage*)handle;
    return message->GetContent();
}

size_t mcp_message_get_content_length(mcp_message_handle_t handle) {
    if (!handle) {
        return 0;
    }
    
    MCP::CMCPMessage* message = (MCP::CMCPMessage*)handle;
    return message->GetContentLength();
}

uint32_t mcp_message_get_id(mcp_message_handle_t handle) {
    if (!handle) {
        return 0;
    }
    
    MCP::CMCPMessage* message = (MCP::CMCPMessage*)handle;
    return message->GetMessageId();
}

const char* mcp_message_get_method(mcp_message_handle_t handle) {
    if (!handle) {
        return nullptr;
    }
    
    MCP::CMCPMessage* message = (MCP::CMCPMessage*)handle;
    return message->GetMethod();
}

bool mcp_message_is_request(mcp_message_handle_t handle) {
    if (!handle) {
        return false;
    }
    
    MCP::CMCPMessage* message = (MCP::CMCPMessage*)handle;
    return message->IsRequest();
}

bool mcp_message_is_response(mcp_message_handle_t handle) {
    if (!handle) {
        return false;
    }
    
    MCP::CMCPMessage* message = (MCP::CMCPMessage*)handle;
    return message->IsResponse();
}

bool mcp_message_is_valid(mcp_message_handle_t handle) {
    if (!handle) {
        return false;
    }
    
    MCP::CMCPMessage* message = (MCP::CMCPMessage*)handle;
    return message->IsValid();
}

esp_err_t mcp_message_serialize(mcp_message_handle_t handle, char* buffer, size_t buffer_size, size_t* output_length) {
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    MCP::CMCPMessage* message = (MCP::CMCPMessage*)handle;
    return message->Serialize(buffer, buffer_size, output_length);
}

} /* extern "C" */