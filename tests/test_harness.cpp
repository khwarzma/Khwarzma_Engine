#include <iostream>
#include <chrono>
#include <string>
#include <vector>
#include <cstdint>

#include "khshield/shield_engine.hpp"
#include "khcomp/comp_engine.hpp"
#include "khcomp/bit_stream.hpp"
#include "khcomp/context_model.hpp"

int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << "  KH-CORE NATIVE BENCHMARK & REAL TEST    " << std::endl;
    std::cout << "==========================================" << std::endl;

    // 1. Benchmark & Test for KhShield Engine
    std::cout << "\n[1] Testing KhShield Moderation Engine..." << std::endl;
    khshield::ShieldEngine shield_engine(khshield::Preset::STRICT);
    shield_engine.add_keyword("test_bad_word");

    std::string sample_text = "هذا نص تجريبي لاختبار سرعة محرك khshield على السيرفر ومطابقة النصوص بشكل آمن ومباشر.";
    
    auto start_shield = std::chrono::high_resolution_clock::now();
    
    auto report = shield_engine.analyze_text(sample_text);
    
    auto end_shield = std::chrono::high_resolution_clock::now();
    auto shield_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_shield - start_shield).count();
    
    std::cout << "    - Input Text Length: " << sample_text.size() << " bytes" << std::endl;
    std::cout << "    - Is Safe: " << (report.is_safe ? "YES" : "NO") << std::endl;
    std::cout << "    - Execution Time: " << shield_duration << " us (microseconds)" << std::endl;

    // 2. Benchmark & Test for KhComp Core Arithmetic Compression Pipeline
    std::cout << "\n[2] Testing KhComp Arithmetic Core..." << std::endl;
    
    khcomp::core::ArithmeticEncoder encoder;
    khcomp::core::BitStreamWriter writer;
    khcomp::core::ContextModel model;

    encoder.set_writer(&writer);
    
    std::vector<uint8_t> test_data = {0x10, 0x20, 0x30, 0x40, 0x50}; 
    
    auto start_comp = std::chrono::high_resolution_clock::now();
    
    for (uint8_t byte : test_data) {
        encoder.encode_symbol(byte, model);
    }
    encoder.flush();
    
    auto end_comp = std::chrono::high_resolution_clock::now();
    auto comp_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_comp - start_comp).count();

    std::cout << "    - Encoded Symbol Count: " << test_data.size() << std::endl;
    std::cout << "    - Execution Time: " << comp_duration << " us (microseconds)" << std::endl;

    std::cout << "\n==========================================" << std::endl;
    std::cout << "Status: Native Harness Test Executed Successfully." << std::endl;
    std::cout << "==========================================" << std::endl;

    return 0;
}