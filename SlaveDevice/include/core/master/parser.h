/** @file include/core/master/parser.h
 *  @brief 上位机命令参数解析接口定义。
 */
#ifndef CORE_MASTER_PARSER_H
#define CORE_MASTER_PARSER_H

#include <Arduino.h>
#include <stdint.h>

namespace MasterParser {
/**
 * @brief 解析 int16 文本参数，要求整串都是合法十进制整数。
 * @param text 输入文本。
 * @param value 写入解析结果。
 * @return 解析成功返回 true。
 */
bool parseInt16(const char* text, int16_t& value);
/**
 * @brief 解析 float 文本参数，要求整串都是合法浮点数。
 * @param text 输入文本。
 * @param value 写入解析结果。
 * @return 解析成功返回 true。
 */
bool parseFloat32(const char* text, float& value);
/**
 * @brief 解析 0/1/ON/OFF 布尔参数。
 * @param text 输入文本。
 * @param value 写入解析结果。
 * @return 解析成功返回 true。
 */
bool parseBool01(const char* text, bool& value);
}

#endif
