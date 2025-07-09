#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "dhc.h"
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstring>
#include "mbedtls/base64.h"

#define TAG "MAIN"

// Function to convert base64 to binary
bool base64_decode(const char* input, std::vector<uint8_t>& output) {
    size_t output_len;
    size_t input_len = strlen(input);
    // Calculate required output buffer size
    if(mbedtls_base64_decode(nullptr, 0, &output_len, (const unsigned char*)input, input_len) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        ESP_LOGE(TAG, "Failed to calculate base64 decode length");
        return false;
    }
    // Resize output vector
    output.resize(output_len);
    // Perform actual decoding
    if(mbedtls_base64_decode(output.data(), output_len, &output_len, (const unsigned char*)input, input_len) != 0) {
        ESP_LOGE(TAG, "Failed to decode base64 data");
        return false;
    }
    output.resize(output_len);  // Adjust to actual size
    return true;
}

// Function to print data in hex format
void print_hex(const uint8_t* data, size_t len) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    for(size_t i = 0; i < len; i++) {
        ss << std::setw(2) << static_cast<int>(data[i]);
        if((i + 1) % 16 == 0) {
            ss << "\n";
        } else {
            ss << " ";
        }
    }
    
    ESP_LOGI(TAG, "\nHex dump:\n%s", ss.str().c_str());
}

extern "C" void app_main(void)
{
    // Writable buffer for base64 input (modifiable in code)
    static char base64_data[110000] = "";
    static char base64_output_chunk[1200] = ""; // Output buffer for compressed base64 chunk (base64 expands by ~33%)

    size_t base64_len = strlen(base64_data);
    size_t base64_pos = 0;
    size_t chunk_index = 0;
    DHC compressor;
    while (base64_pos < base64_len) {
        // Find the next chunk of base64 (must be a multiple of 4 for base64 decode)
        size_t chunk_b64_len = 1024; // Smaller chunk for embedded RAM
        if (base64_pos + chunk_b64_len > base64_len) {
            chunk_b64_len = base64_len - base64_pos;
        }
        // Ensure chunk_b64_len is a multiple of 4
        chunk_b64_len = (chunk_b64_len / 4) * 4;
        if (chunk_b64_len == 0) break;

        ESP_LOGI(TAG, "Decoding chunk %d: base64_pos=%d, chunk_b64_len=%d", (int)chunk_index, (int)base64_pos, (int)chunk_b64_len);
        ESP_LOGI(TAG, "First 32 base64 chars: %.32s", &base64_data[base64_pos]);
        ESP_LOGI(TAG, "Free heap before compression: %lu", (unsigned long)esp_get_free_heap_size());

        // Decode base64 chunk to binary
        static uint8_t binary_chunk[1024]; // Match chunk size, now static to avoid stack overflow
        size_t binary_chunk_len = sizeof(binary_chunk);
        int ret = mbedtls_base64_decode(binary_chunk, binary_chunk_len, &binary_chunk_len,
                                        (const unsigned char*)&base64_data[base64_pos], chunk_b64_len);
        if (ret != 0) {
            ESP_LOGE(TAG, "Base64 decode failed for chunk %d", (int)chunk_index);
            break;
        }

        // Compress the binary chunk
        static uint8_t compressed_chunk[1024]; // Match chunk size, now static to avoid stack overflow
        size_t compressed_chunk_len = sizeof(compressed_chunk);
        if (compressor.compress(binary_chunk, binary_chunk_len, compressed_chunk, &compressed_chunk_len)) {
            // Calculate and log compression ratio
            float compression_ratio = (float)compressed_chunk_len / (float)binary_chunk_len;
            ESP_LOGI(TAG, "Chunk %d: Original size: %d, Compressed size: %d, Compression ratio: %.2f%%", (int)chunk_index, (int)binary_chunk_len, (int)compressed_chunk_len, compression_ratio * 100.0f);
            // Encode compressed chunk to base64
            size_t base64_output_chunk_len = sizeof(base64_output_chunk);
            int enc_ret = mbedtls_base64_encode((unsigned char*)base64_output_chunk, base64_output_chunk_len, &base64_output_chunk_len,
                                                compressed_chunk, compressed_chunk_len);
            if (enc_ret == 0) {
                ESP_LOGI(TAG, "Compressed chunk %d (base64):\n%s", (int)chunk_index, base64_output_chunk);
            } else {
                ESP_LOGE(TAG, "Base64 encoding failed for compressed chunk %d", (int)chunk_index);
            }
        } else {
            ESP_LOGE(TAG, "Compression failed for chunk %d", (int)chunk_index);
        }

        base64_pos += chunk_b64_len;
        chunk_index++;
    }

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
} 