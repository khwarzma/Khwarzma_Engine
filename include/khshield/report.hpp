#ifndef KHSHIELD_REPORT_HPP
#define KHSHIELD_REPORT_HPP

#include <string>

namespace khshield {

struct ContentReport {
    bool is_safe{true};
    float risk_score{0.0f}; // Range: [0.0, 1.0]
    std::string violation_type{"NONE"}; // e.g., "EXPLICIT_BODY", "MUSIC_DETECTED", "PROFANITY"
    
    // Sub-reports
    struct TextDetails { 
        bool flagged{false}; 
        std::string matched_pattern; 
    } text;

    struct VisualDetails { 
        bool flagged{false}; 
        float skin_percentage{0.0f}; 
    } visual;

    struct AudioDetails { 
        bool flagged{false}; 
        bool corrupted{false}; 
        float bpm{0.0f}; 
    } audio;
};

} // namespace khshield

#endif // KHSHIELD_REPORT_HPP