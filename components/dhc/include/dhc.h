#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Huffman tree node
struct HuffmanNode {
    int16_t value;
    size_t frequency;
    std::shared_ptr<HuffmanNode> left;
    std::shared_ptr<HuffmanNode> right;
    
    HuffmanNode(int16_t val, size_t freq) : value(val), frequency(freq), left(nullptr), right(nullptr) {}
};

class DHC {
public:
    DHC();
    ~DHC();

    bool compress(const uint8_t* input, size_t input_size, uint8_t* output, size_t* output_size);
    bool decompress(const uint8_t* input, size_t input_size, uint8_t* output, size_t* output_size);
    bool compress_file(const char* input_file, const char* output_file);
    bool decompress_file(const char* input_file, const char* output_file);

    // New methods for chunked processing
    static const size_t CHUNK_SIZE = 4096;  // Process 4KB at a time

private:
    static const uint16_t MAGIC = 0x4448;  // "DH" in ASCII as magic number
    std::vector<int16_t> lastDeltaValues;
    std::unordered_map<int16_t, std::string> lastHuffmanCodes;

    std::vector<int16_t> computeDeltaValues(const std::vector<uint16_t>& data);
    std::vector<uint16_t> reconstructFromDelta(const std::vector<int16_t>& deltaValues);
    std::unordered_map<int16_t, std::string> buildHuffmanCodes(const std::vector<int16_t>& deltaValues);
    void generateCodes(const std::shared_ptr<HuffmanNode>& node, std::string code,
                      std::unordered_map<int16_t, std::string>& codes);
    
    // Helper methods for chunked processing
    bool process_file_chunk(FILE* in_file, FILE* out_file, uint8_t* buffer, size_t buffer_size);
    bool process_compressed_chunk(FILE* in_file, FILE* out_file, uint8_t* buffer, size_t buffer_size);
}; 