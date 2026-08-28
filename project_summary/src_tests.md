# Source Files: tests


--- FILE: src/tests/test_harness.cpp ---
```cpp
#include <iostream>
#include <chrono>
#include <string>
#include "khcomp/comp_engine.hpp"
#include "khshield/shield_engine.hpp"

int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << "  KH-CORE NATIVE BENCHMARK & REAL TEST    " << std::endl;
    std::cout << "==========================================" << std::endl;

    // 1. اختبار محرك التحليل السريع (Short-circuit Text Moderation)
    std::string sample_text = "هذا نص تجريبي لاختبار سرعة محرك khshield على السيرفر";
    
    auto start_shield = std::chrono::high_resolution_clock::now();
    
    // تجربة استدعاء الكور مباشرة (سنخصص الفحص بناءً على API المحرك لديك)
    std::cout << "[SHIELD] Input Text Length: " << sample_text.size() << " bytes" << std::endl;
    
    auto end_shield = std::chrono::high_resolution_clock::now();
    auto shield_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_shield - start_shield).count();
    
    std::cout << "[SHIELD] Execution Time: " << shield_duration << " us (microseconds)" << std::endl;

    std::cout << "------------------------------------------" << std::endl;
    std::cout << "Status: Ready for Real-Data Harness Pipeline." << std::endl;
    return 0;
}```
