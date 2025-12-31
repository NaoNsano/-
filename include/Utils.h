/*
    这是一个工具函数头文件，里面会实现一些转换输入的工具函数
    直接在.h里实现了因为不怎么长
*/
#pragma once

#include <string>
#include <iostream>
#include <cstring>
#include <cassert>
#include <cmath>
#include <exception>

/*
    举例：输入："[0:00:08]"，输出：8000 (毫秒)
*/
inline long long ParseTimestamp(const std::string& time_str){
    std::size_t length = time_str.size();
    assert(time_str[0] == '[' && time_str[length-1] == ']');    // 检查输入是否大致合法
    std::size_t first_pos = time_str.find(':');
    std::size_t second_pos = time_str.rfind(':');
    assert(first_pos != std::string::npos && second_pos != std::string::npos);
    assert(first_pos != second_pos);  // 确保有两个不同的冒号

    long long curr_time = 0;

    // 解析小时部分：[时:分:秒]
    std::size_t sub_length_hour = (first_pos - 1) - 1 + 1;
    std::string sub_hour = time_str.substr(1, first_pos - 1);
    try {
        long long hours = std::stoll(sub_hour);
        curr_time += hours * 3600LL;
    } catch (const std::exception& e) {
        std::cerr << "Conversion error (hour): " << e.what() << std::endl;
        throw;
    }

    std::size_t sub_length_min = (second_pos - 1) - (first_pos + 1) + 1;
    std::string sub_min = time_str.substr(first_pos + 1, second_pos - first_pos - 1);
    long long minutes;
    try {
        minutes = std::stoll(sub_min);
    } catch (const std::exception& e) {
        std::cerr << "Conversion error (minutes): " << e.what() << std::endl;
        throw;
    }
    assert(0LL <= minutes && minutes <= 60LL);
    curr_time += minutes * 60LL;

    // 解析秒部分（支持小数秒）
    std::size_t sub_length_sec = (length - 2) - (second_pos + 1) + 1;
    std::string sub_sec = time_str.substr(second_pos + 1, length - second_pos - 2);
    try {
        double seconds = std::stod(sub_sec);
        assert(seconds >= 0.0 && seconds < 60.0);  // 检查秒数是否合法
        // 将秒转换为毫秒（注意curr_time目前是秒数，需要转换为毫秒）
        // 先计算总秒数，最后统一转换为毫秒
        double total_seconds = static_cast<double>(curr_time) + seconds;
        return static_cast<long long>(total_seconds * 1000.0);
    } catch (const std::exception& e) {
        std::cerr << "Conversion error (seconds): " << e.what() << std::endl;
        throw;
    }
}

inline std::string GetTimestampString(long long timestamp){
    throw std::runtime_error("Not Finished yet");
}

inline std::string ExtractTimeTag(const std::string& input) {
    // 查找第一个']'的位置
    size_t pos = input.find(']');
    size_t start_pos = input.find('[');
    
    // 如果找到了']'，返回从开始到']'的子串（包括']'）
    if (pos != std::string::npos && start_pos != std::string::npos) {
        return input.substr(start_pos, pos + 1);
    }
    
    // 如果没有找到']'，返回错误，错误会在识别时间中被抛出
    return "error";
}

/*
    线性回归辅助结构
*/
struct RegressionStats {
    long long sum_x = 0;    // Σx (时间)
    long long sum_y = 0;    // Σy (词频)
    long long sum_xy = 0;   // Σxy
    long long sum_xx = 0;   // Σx^2
    int n = 0;              // 数据点个数 (桶数)
};