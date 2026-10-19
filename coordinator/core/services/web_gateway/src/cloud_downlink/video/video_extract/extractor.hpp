#pragma once
#include <zmq.hpp>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>

class VideoTimeRangeExtractor {
public:
    explicit VideoTimeRangeExtractor();
    std::string extract_video_range(const std::string& channel, const std::string& start_time, const std::string& end_time);

private:
    struct VideoFileInfo {
        std::filesystem::path path;
        std::chrono::system_clock::time_point start_time;
        int duration_seconds;
        std::chrono::system_clock::time_point end_time;
    };
    std::chrono::system_clock::time_point parse_filename_timestamp(const std::string& filename);
    std::chrono::system_clock::time_point parse_time_string(const std::string& time_str);
    
    int get_video_duration(const std::string& video_path);
    
    std::string extract_single_file(const VideoFileInfo& file_info, const std::chrono::system_clock::time_point& start_time, const std::chrono::system_clock::time_point& end_time, const std::string& channel);
    
    std::string extract_multiple_files(const std::vector<VideoFileInfo>& files, const std::chrono::system_clock::time_point& start_time, const std::chrono::system_clock::time_point& end_time, const std::string& channel);
    
    bool extract_with_ffmpeg(const std::string& input_file, const std::string& output_file, int start_seconds, int duration);
    
    bool concat_videos(const std::vector<std::string>& input_files, const std::string& output_file);
};