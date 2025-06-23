#include "dhc.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include <queue>
#include <bitset>
#include <vector>

#define TAG "DHC"

// Custom comparator for priority queue
struct CompareNodes {
    bool operator()(const std::shared_ptr<HuffmanNode>& a, const std::shared_ptr<HuffmanNode>& b) {
        return a->frequency > b->frequency;
    }
};

// Implementation of the DHC class
DHC::DHC() {
}

DHC::~DHC() {
}

std::vector<int16_t> DHC::computeDeltaValues(const std::vector<uint16_t>& data) {
    std::vector<int16_t> deltaValues(data.size());
    deltaValues[0] = static_cast<int16_t>(data[0]);
    
    for (size_t i = 1; i < data.size(); i++) {
        deltaValues[i] = static_cast<int16_t>(data[i]) - static_cast<int16_t>(data[i-1]);
    }
    return deltaValues;
}

std::vector<uint16_t> DHC::reconstructFromDelta(const std::vector<int16_t>& deltaValues) {
    std::vector<uint16_t> originalData(deltaValues.size());
    int32_t accumulator = deltaValues[0];
    originalData[0] = static_cast<uint16_t>(accumulator);
    
    for (size_t i = 1; i < deltaValues.size(); i++) {
        accumulator += deltaValues[i];
        originalData[i] = static_cast<uint16_t>(accumulator);
    }
    return originalData;
}

void DHC::generateCodes(const std::shared_ptr<HuffmanNode>& node, std::string code,
                       std::unordered_map<int16_t, std::string>& codes) {
    if (!node) return;
    
    if (!node->left && !node->right) {
        codes[node->value] = code;
    }
    
    generateCodes(node->left, code + "0", codes);
    generateCodes(node->right, code + "1", codes);
}

std::unordered_map<int16_t, std::string> DHC::buildHuffmanCodes(const std::vector<int16_t>& deltaValues) {
    // Calculate frequencies
    std::unordered_map<int16_t, size_t> frequencies;
    for (const auto& value : deltaValues) {
        frequencies[value]++;
    }

    // Handle case where all values are the same
    if (frequencies.size() == 1) {
        auto value = frequencies.begin()->first;
        return {{value, "0"}};
    }

    // Create priority queue
    std::priority_queue<std::shared_ptr<HuffmanNode>, 
                       std::vector<std::shared_ptr<HuffmanNode>>, 
                       CompareNodes> pq;

    // Add nodes to priority queue
    for (const auto& pair : frequencies) {
        pq.push(std::make_shared<HuffmanNode>(pair.first, pair.second));
    }

    // Build Huffman tree
    while (pq.size() > 1) {
        auto left = pq.top(); pq.pop();
        auto right = pq.top(); pq.pop();
        
        auto parent = std::make_shared<HuffmanNode>(0, left->frequency + right->frequency);
        parent->left = left;
        parent->right = right;
        
        pq.push(parent);
    }

    // Generate Huffman codes
    std::unordered_map<int16_t, std::string> huffmanCodes;
    if (!pq.empty()) {
        generateCodes(pq.top(), "", huffmanCodes);
    }
    
    return huffmanCodes;
}

bool DHC::compress(const uint8_t* input, size_t input_size, uint8_t* output, size_t* output_size) {
    if (!input || !output || !output_size || input_size == 0) {
        ESP_LOGE(TAG, "Invalid input parameters");
        return false;
    }

    // Convert input to uint16_t vector
    std::vector<uint16_t> data(input_size / 2);
    memcpy(data.data(), input, input_size);

    // Compute delta values
    std::vector<int16_t> deltaValues = computeDeltaValues(data);
    lastDeltaValues = deltaValues;

    // Build Huffman codes
    auto huffmanCodes = buildHuffmanCodes(deltaValues);
    lastHuffmanCodes = huffmanCodes;

    // Write magic number
    size_t pos = 0;
    output[pos++] = static_cast<uint8_t>(MAGIC >> 8);
    output[pos++] = static_cast<uint8_t>(MAGIC & 0xFF);

    // Write original size
    uint32_t originalSize = static_cast<uint32_t>(data.size());
    output[pos++] = static_cast<uint8_t>((originalSize >> 24) & 0xFF);
    output[pos++] = static_cast<uint8_t>((originalSize >> 16) & 0xFF);
    output[pos++] = static_cast<uint8_t>((originalSize >> 8) & 0xFF);
    output[pos++] = static_cast<uint8_t>(originalSize & 0xFF);

    // Write compressed data
    std::vector<bool> bits;
    for (const auto& value : deltaValues) {
        const std::string& code = huffmanCodes[value];
        for (char bit : code) {
            bits.push_back(bit == '1');
        }
    }

    // Pack bits into bytes
    size_t bytePos = pos;
    uint8_t currentByte = 0;
    int bitCount = 0;

    for (bool bit : bits) {
        currentByte = (currentByte << 1) | (bit ? 1 : 0);
        bitCount++;
        
        if (bitCount == 8) {
            output[bytePos++] = currentByte;
            currentByte = 0;
            bitCount = 0;
        }
    }

    // Handle remaining bits
    if (bitCount > 0) {
        currentByte <<= (8 - bitCount);
        output[bytePos++] = currentByte;
    }

    *output_size = bytePos;
    return true;
}

bool DHC::decompress(const uint8_t* input, size_t input_size, uint8_t* output, size_t* output_size) {
    if (!input || !output || !output_size || input_size < 6) {
        ESP_LOGE(TAG, "Invalid input parameters");
        return false;
    }

    // Verify magic number
    uint16_t magic = (input[0] << 8) | input[1];
    if (magic != MAGIC) {
        ESP_LOGE(TAG, "Invalid magic number");
        return false;
    }

    // Read original size
    uint32_t originalSize = (input[2] << 24) | (input[3] << 16) | (input[4] << 8) | input[5];
    if (*output_size < originalSize * 2) {
        ESP_LOGE(TAG, "Output buffer too small");
        return false;
    }

    // Reconstruct Huffman tree from codes
    auto huffmanCodes = lastHuffmanCodes;
    std::shared_ptr<HuffmanNode> root = std::make_shared<HuffmanNode>(0, 0);
    for (const auto& pair : huffmanCodes) {
        std::shared_ptr<HuffmanNode> current = root;
        for (char bit : pair.second) {
            if (bit == '0') {
                if (!current->left) {
                    current->left = std::make_shared<HuffmanNode>(0, 0);
                }
                current = current->left;
            } else {
                if (!current->right) {
                    current->right = std::make_shared<HuffmanNode>(0, 0);
                }
                current = current->right;
            }
        }
        current->value = pair.first;
    }

    // Decode bits to delta values
    std::vector<int16_t> deltaValues;
    std::shared_ptr<HuffmanNode> current = root;
    
    for (size_t i = 6; i < input_size; i++) {
        std::bitset<8> bits(input[i]);
        for (int j = 7; j >= 0; j--) {
            if (bits[j]) {
                current = current->right;
            } else {
                current = current->left;
            }
            
            if (!current->left && !current->right) {
                deltaValues.push_back(current->value);
                current = root;
                
                if (deltaValues.size() == originalSize) {
                    break;
                }
            }
        }
        if (deltaValues.size() == originalSize) {
            break;
        }
    }

    // Reconstruct original values
    std::vector<uint16_t> originalData = reconstructFromDelta(deltaValues);
    
    // Copy to output buffer
    memcpy(output, originalData.data(), originalData.size() * sizeof(uint16_t));
    *output_size = originalData.size() * sizeof(uint16_t);

    return true;
}

bool DHC::compress_file(const char* input_file, const char* output_file) {
    FILE* in_file = fopen(input_file, "rb");
    if (!in_file) {
        ESP_LOGE(TAG, "Failed to open input file: %s", input_file);
        return false;
    }

    FILE* out_file = fopen(output_file, "wb");
    if (!out_file) {
        ESP_LOGE(TAG, "Failed to open output file: %s", output_file);
        fclose(in_file);
        return false;
    }

    // Write magic number
    uint8_t magic[2] = {static_cast<uint8_t>(MAGIC >> 8), static_cast<uint8_t>(MAGIC & 0xFF)};
    fwrite(magic, 1, 2, out_file);

    // Get file size
    fseek(in_file, 0, SEEK_END);
    uint32_t file_size = ftell(in_file);
    fseek(in_file, 0, SEEK_SET);

    // Write original file size
    uint8_t size_bytes[4] = {
        static_cast<uint8_t>((file_size >> 24) & 0xFF),
        static_cast<uint8_t>((file_size >> 16) & 0xFF),
        static_cast<uint8_t>((file_size >> 8) & 0xFF),
        static_cast<uint8_t>(file_size & 0xFF)
    };
    fwrite(size_bytes, 1, 4, out_file);

    // Allocate buffer for processing chunks
    uint8_t* buffer = (uint8_t*)malloc(CHUNK_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        fclose(in_file);
        fclose(out_file);
        return false;
    }

    bool success = true;
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, in_file)) > 0) {
        if (!process_file_chunk(in_file, out_file, buffer, bytes_read)) {
            success = false;
            break;
        }
    }

    free(buffer);
    fclose(in_file);
    fclose(out_file);

    if (!success) {
        remove(output_file);  // Delete the output file if compression failed
    }

    return success;
}

bool DHC::process_file_chunk(FILE* in_file, FILE* out_file, uint8_t* buffer, size_t buffer_size) {
    // Convert input to uint16_t vector
    std::vector<uint16_t> data(buffer_size / 2);
    memcpy(data.data(), buffer, buffer_size);

    // Compute delta values
    std::vector<int16_t> deltaValues = computeDeltaValues(data);
    lastDeltaValues = deltaValues;

    // Build Huffman codes
    auto huffmanCodes = buildHuffmanCodes(deltaValues);
    lastHuffmanCodes = huffmanCodes;

    // Write compressed data
    std::vector<bool> bits;
    for (const auto& value : deltaValues) {
        const std::string& code = huffmanCodes[value];
        for (char bit : code) {
            bits.push_back(bit == '1');
        }
    }

    // Pack bits into bytes
    uint8_t currentByte = 0;
    int bitCount = 0;

    for (bool bit : bits) {
        currentByte = (currentByte << 1) | (bit ? 1 : 0);
        bitCount++;
        
        if (bitCount == 8) {
            fwrite(&currentByte, 1, 1, out_file);
            currentByte = 0;
            bitCount = 0;
        }
    }

    // Handle remaining bits
    if (bitCount > 0) {
        currentByte <<= (8 - bitCount);
        fwrite(&currentByte, 1, 1, out_file);
    }

    return true;
}

bool DHC::decompress_file(const char* input_file, const char* output_file) {
    FILE* in_file = fopen(input_file, "rb");
    if (!in_file) {
        ESP_LOGE(TAG, "Failed to open input file: %s", input_file);
        return false;
    }

    // Read and verify magic number
    uint8_t magic[2];
    if (fread(magic, 1, 2, in_file) != 2 ||
        ((magic[0] << 8) | magic[1]) != MAGIC) {
        ESP_LOGE(TAG, "Invalid magic number");
        fclose(in_file);
        return false;
    }

    // Read original file size
    uint8_t size_bytes[4];
    if (fread(size_bytes, 1, 4, in_file) != 4) {
        ESP_LOGE(TAG, "Failed to read file size");
        fclose(in_file);
        return false;
    }

    FILE* out_file = fopen(output_file, "wb");
    if (!out_file) {
        ESP_LOGE(TAG, "Failed to open output file: %s", output_file);
        fclose(in_file);
        return false;
    }

    // Allocate buffer for processing chunks
    uint8_t* buffer = (uint8_t*)malloc(CHUNK_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        fclose(in_file);
        fclose(out_file);
        return false;
    }

    bool success = true;
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, CHUNK_SIZE, in_file)) > 0) {
        if (!process_compressed_chunk(in_file, out_file, buffer, bytes_read)) {
            success = false;
            break;
        }
    }

    free(buffer);
    fclose(in_file);
    fclose(out_file);

    if (!success) {
        remove(output_file);  // Delete the output file if decompression failed
    }

    return success;
}

bool DHC::process_compressed_chunk(FILE* in_file, FILE* out_file, uint8_t* buffer, size_t buffer_size) {
    std::vector<bool> bits;
    for (size_t i = 0; i < buffer_size; i++) {
        std::bitset<8> byte_bits(buffer[i]);
        for (int j = 7; j >= 0; j--) {
            bits.push_back(byte_bits[j]);
        }
    }

    // Reconstruct Huffman tree from codes
    auto huffmanCodes = lastHuffmanCodes;
    std::shared_ptr<HuffmanNode> root = std::make_shared<HuffmanNode>(0, 0);
    for (const auto& pair : huffmanCodes) {
        std::shared_ptr<HuffmanNode> current = root;
        for (char bit : pair.second) {
            if (bit == '0') {
                if (!current->left) {
                    current->left = std::make_shared<HuffmanNode>(0, 0);
                }
                current = current->left;
            } else {
                if (!current->right) {
                    current->right = std::make_shared<HuffmanNode>(0, 0);
                }
                current = current->right;
            }
        }
        current->value = pair.first;
    }

    // Decode bits
    std::vector<int16_t> deltaValues;
    std::shared_ptr<HuffmanNode> current = root;
    for (bool bit : bits) {
        if (bit) {
            current = current->right;
        } else {
            current = current->left;
        }

        if (!current->left && !current->right) {
            deltaValues.push_back(current->value);
            current = root;
        }
    }

    // Reconstruct original values
    std::vector<uint16_t> originalData = reconstructFromDelta(deltaValues);

    // Write to output file
    fwrite(originalData.data(), sizeof(uint16_t), originalData.size(), out_file);

    return true;
} 