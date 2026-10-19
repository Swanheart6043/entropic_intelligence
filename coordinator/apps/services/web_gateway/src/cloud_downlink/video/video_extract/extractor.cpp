#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <spdlog/spdlog.h>
#include "extractor.hpp"

namespace fs = std::filesystem;

VideoTimeRangeExtractor::VideoTimeRangeExtractor() {
    
}

// 解析文件名中的时间戳: high_definition_20260114_223432.mp4 -> 时间点
std::chrono::system_clock::time_point VideoTimeRangeExtractor::parse_filename_timestamp(const std::string& filename) {
    // filename 格式: channel_YYYYMMDD_HHMMSS.mp4
    // 例如: high_definition_20260114_223432.mp4
    
    // 找到最后一个点（扩展名前）
    size_t dot_pos = filename.rfind('.');
    if (dot_pos == std::string::npos) {
        throw std::runtime_error("无效的文件名格式：缺少扩展名");
    }
    
    // 从后往前找两个下划线
    std::string name_without_ext = filename.substr(0, dot_pos);
    // name_without_ext: high_definition_20260114_223432
    
    size_t last_underscore = name_without_ext.rfind('_');
    if (last_underscore == std::string::npos) {
        throw std::runtime_error("无效的文件名格式：缺少时间下划线");
    }
    
    size_t second_last_underscore = name_without_ext.rfind('_', last_underscore - 1);
    if (second_last_underscore == std::string::npos) {
        throw std::runtime_error("无效的文件名格式：缺少日期下划线");
    }
    
    // 提取日期和时间
    std::string date_str = name_without_ext.substr(second_last_underscore + 1, 8);  // 20260114
    std::string time_str = name_without_ext.substr(last_underscore + 1, 6);         // 223432
    
    SPDLOG_DEBUG("解析文件名 {}: 日期={}, 时间={}", filename, date_str, time_str);
    
    std::tm tm = {};
    tm.tm_year = std::stoi(date_str.substr(0, 4)) - 1900;
    tm.tm_mon = std::stoi(date_str.substr(4, 2)) - 1;
    tm.tm_mday = std::stoi(date_str.substr(6, 2));
    tm.tm_hour = std::stoi(time_str.substr(0, 2));
    tm.tm_min = std::stoi(time_str.substr(2, 2));
    tm.tm_sec = std::stoi(time_str.substr(4, 2));
    
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// 解析消息中的时间字符串: "2025-09-17 10:00:00" -> 时间点
std::chrono::system_clock::time_point VideoTimeRangeExtractor::parse_time_string(const std::string& time_str) {
    std::tm tm = {};
    std::istringstream ss(time_str);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    if (ss.fail()) {
        throw std::runtime_error("时间格式解析失败: " + time_str);
    }
    
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// 获取视频文件的时长（秒）
int VideoTimeRangeExtractor::get_video_duration(const std::string& video_path) {
    // 使用 ffprobe 获取视频时长
    std::string cmd = "ffprobe -v error -show_entries format=duration ""-of default=noprint_wrappers=1:nokey=1 \"" + video_path + "\"";
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        SPDLOG_ERROR("无法执行 ffprobe 命令");
        return 0;
    }
    
    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    
    try {
        return static_cast<int>(std::stof(result));
    } catch (...) {
        SPDLOG_ERROR("解析视频时长失败: {}", result);
        return 0;
    }
}

std::string VideoTimeRangeExtractor::extract_video_range(const std::string& channel, const std::string& start_time_str, const std::string& end_time_str) {
    try {
        // 解析目标时间段
        auto start_time = parse_time_string(start_time_str); // "start_time": "2025-09-17 10:00:00",
        auto end_time = parse_time_string(end_time_str); // "end_time": "2025-09-17 10:30:00",
        SPDLOG_INFO("截取视频时间段: {} 到 {}", start_time_str, end_time_str);
        
        // 找到通道目录
        fs::path channel_dir = fs::path("/datas/recordings") / channel;
        if (!fs::exists(channel_dir)) {
            SPDLOG_ERROR("通道目录不存在: {}", channel_dir.string());
            return "";
        }
        
        std::vector<VideoFileInfo> video_files;
        for (const auto& entry : fs::directory_iterator(channel_dir)) {
            if (entry.path().extension() == ".mp4") {
                try {
                    auto file_start_time = parse_filename_timestamp(entry.path().filename().string());
                    int duration = get_video_duration(entry.path().string());
                    
                    VideoFileInfo info;
                    info.path = entry.path();
                    info.start_time = file_start_time;
                    info.duration_seconds = duration;
                    info.end_time = file_start_time + std::chrono::seconds(duration);
                    video_files.push_back(info);
                    
                } catch (const std::exception& e) {
                    SPDLOG_WARN("跳过文件 {}: {}", entry.path().string(), e.what());
                }
            }
        }
        
        // 按开始时间排序
        std::sort(video_files.begin(), video_files.end(), [](const VideoFileInfo& a, const VideoFileInfo& b) {
            return a.start_time < b.start_time;
        });
        
        // 找到包含目标时间段的文件
        std::vector<VideoFileInfo> relevant_files;
        for (const auto& file : video_files) {
            // 文件时间段与目标时间段有交集
            if (file.start_time <= end_time && file.end_time >= start_time) {
                relevant_files.push_back(file);
            }
        }
        
        if (relevant_files.empty()) {
            SPDLOG_ERROR("未找到包含时间段 {} 到 {} 的视频文件", start_time_str, end_time_str);
            return "";
        }
        SPDLOG_INFO("找到 {} 个相关视频文件", relevant_files.size());
        
        // 截取视频片段
        if (relevant_files.size() == 1) {
            // 单个文件，直接截取
            return extract_single_file(relevant_files[0], start_time, end_time, channel);
        } 
        // 多个文件，需要分段截取并合并
        return extract_multiple_files(relevant_files, start_time, end_time, channel);

    } catch (const std::exception& e) {
        SPDLOG_ERROR("视频截取失败: {}", e.what());
        return "";
    }
}

std::string VideoTimeRangeExtractor::extract_single_file(const VideoFileInfo& file_info, const std::chrono::system_clock::time_point& start_time, const std::chrono::system_clock::time_point& end_time, const std::string& channel) {    
    // 计算相对于视频文件的偏移量（秒）
    auto start_offset = std::chrono::duration_cast<std::chrono::seconds>(start_time - file_info.start_time).count();
    auto end_offset = std::chrono::duration_cast<std::chrono::seconds>(end_time - file_info.start_time).count();
    
    // 确保偏移量在有效范围内
    start_offset = std::max(0L, start_offset);
    end_offset = std::min(static_cast<long>(file_info.duration_seconds), end_offset);
    
    int duration = end_offset - start_offset;
    
    if (duration <= 0) {
        SPDLOG_ERROR("计算的视频时长无效: {} 秒", duration);
        return "";
    }
    
    // 构建输出文件路径
    fs::path output_dir = "/datas/extracted";
    fs::create_directories(output_dir);
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream filename_stream;
    filename_stream << channel << "_extracted_" << timestamp << ".mp4";
    
    fs::path output_file = output_dir / filename_stream.str();
    
    // 调用 ffmpeg 截取
    SPDLOG_INFO("从文件 {} 截取 {}秒 到 {}秒 (时长: {}秒)", file_info.path.filename().string(), start_offset, end_offset, duration);
    
    bool success = extract_with_ffmpeg(
        file_info.path.string(),
        output_file.string(),
        start_offset,
        duration
    );
    
    if (success) {
        SPDLOG_INFO("视频截取成功: {}", output_file.string());
        return output_file.string(); // 返回文件路径
    }

    return "";
}

std::string VideoTimeRangeExtractor::extract_multiple_files(const std::vector<VideoFileInfo>& files, const std::chrono::system_clock::time_point& start_time, const std::chrono::system_clock::time_point& end_time, const std::string& channel) {    
    // 临时文件列表
    std::vector<std::string> temp_files;
    fs::path temp_dir = "/tmp/video_extract";
    fs::create_directories(temp_dir);
    
    // 对每个文件截取相应部分
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& file_info = files[i];
        
        // 计算这个文件需要截取的时间段
        auto clip_start = std::max(start_time, file_info.start_time);
        auto clip_end = std::min(end_time, file_info.end_time);
        
        auto start_offset = std::chrono::duration_cast<std::chrono::seconds>(clip_start - file_info.start_time).count();
        auto end_offset = std::chrono::duration_cast<std::chrono::seconds>(clip_end - file_info.start_time).count();
        
        int duration = end_offset - start_offset;
        if (duration <= 0) continue;
        
        // 临时输出文件
        std::string temp_filename = fmt::format("part_{}.mp4", i);
        fs::path temp_output = temp_dir / temp_filename;
        temp_files.push_back(temp_output.string());
        SPDLOG_INFO("截取片段 {}: 文件 {}, 偏移 {}秒, 时长 {}秒", i, file_info.path.filename().string(), start_offset, duration);

        bool success = extract_with_ffmpeg(file_info.path.string(), temp_output.string(), start_offset, duration);
        if (!success) {
            SPDLOG_ERROR("片段 {} 截取失败", i);
            // 清理临时文件
            for (const auto& temp : temp_files) {
                fs::remove(temp);
            }
            return "";
        }
    }
    
    // 合并所有片段
    if (temp_files.empty()) {
        SPDLOG_ERROR("没有成功截取的片段");
        return "";
    }
    
    // 构建最终输出文件
    fs::path output_dir = "/datas/extracted";
    fs::create_directories(output_dir);
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream filename_stream;
    filename_stream << channel << "_extracted_" << timestamp << ".mp4";
    fs::path output_file = output_dir / filename_stream.str();
    
    // 使用 concat demuxer 合并视频
    bool success = concat_videos(temp_files, output_file.string());
    
    // 清理临时文件
    for (const auto& temp : temp_files) {
        fs::remove(temp);
    }
    
    if (success) {
        SPDLOG_INFO("视频截取并合并成功: {}", output_file.string());
        return output_file.string(); // 返回文件路径
    }

    return "";
}

bool VideoTimeRangeExtractor::concat_videos(const std::vector<std::string>& input_files, const std::string& output_file) {
    // 创建 concat 文件列表
    fs::path concat_file = "/tmp/video_extract/concat_list.txt";
    std::ofstream ofs(concat_file.string());
    
    for (const auto& file : input_files) {
        ofs << "file '" << file << "'\n";
    }
    ofs.close();
    
    // 使用 ffmpeg concat demuxer 合并
    std::ostringstream cmd;
    cmd << "ffmpeg -y "
        << "-f concat "
        << "-safe 0 "
        << "-i \""
        << concat_file.string() 
        << "\" "
        << "-c copy "
        << "\"" 
        << output_file 
        << "\" " 
        << "2>&1";
    SPDLOG_INFO("合并视频命令: {}", cmd.str());
    
    int result = std::system(cmd.str().c_str());
    fs::remove(concat_file);
    if (result == 0) {
        SPDLOG_INFO("视频合并成功");
        return true;
    }
    
    SPDLOG_ERROR("视频合并失败，返回码: {}", result);
    return false;
}

bool VideoTimeRangeExtractor::extract_with_ffmpeg(const std::string& input_file, const std::string& output_file, int start_seconds, int duration) {
    std::ostringstream cmd;
    cmd << "ffmpeg -y "
        << "-ss " << start_seconds << " "
        << "-i \"" << input_file << "\" "
        << "-t " << duration << " "
        << "-c copy "
        << "-avoid_negative_ts make_zero "
        << "\"" << output_file << "\" "
        << "2>&1";
    
    SPDLOG_INFO("执行FFmpeg命令: {}", cmd.str());
    
    int result = std::system(cmd.str().c_str());
    
    if (result == 0) {
        return true;
    }
    
    SPDLOG_ERROR("FFmpeg执行失败，返回码: {}", result);
    return false;
}
