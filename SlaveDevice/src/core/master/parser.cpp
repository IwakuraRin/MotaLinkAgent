/** @file parser.cpp
 *  @brief 上位机命令参数解析实现。
 */
#include "core/master/parser.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool MasterParser::parseInt16(const char* text, int16_t& value) {
    if (text == nullptr || *text == 0) {
        return false;
    }

    char* end = nullptr;
    const long parsed = strtol(text, &end, 10);
    if (end == text || *end != 0 || parsed < INT16_MIN || parsed > INT16_MAX) {
        return false;
    }

    value = static_cast<int16_t>(parsed);
    return true;
}

bool MasterParser::parseFloat32(const char* text, float& value) {
    if (text == nullptr || *text == 0) {
        return false;
    }

    char* end = nullptr;
    const double parsed = strtod(text, &end);
    if (end == text || *end != 0) {
        return false;
    }

    value = static_cast<float>(parsed);
    return true;
}

bool MasterParser::parseBool01(const char* text, bool& value) {
    if (text == nullptr) {
        return false;
    }
    if (strcmp(text, "1") == 0 || strcmp(text, "ON") == 0 || strcmp(text, "on") == 0) {
        value = true;
        return true;
    }
    if (strcmp(text, "0") == 0 || strcmp(text, "OFF") == 0 || strcmp(text, "off") == 0) {
        value = false;
        return true;
    }
    return false;
}
