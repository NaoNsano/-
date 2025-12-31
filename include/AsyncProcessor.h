#pragma once
#include "Analyzer.h"
#include <thread>
#include <vector>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <iostream>

class AsyncProcessor {
private:
    Analyzer& analyzer_; // 引用核心分析器
    int batch_size_ = 10;
    
    // --- 线程安全队列定义 ---
    struct SafeQueue {
        std::queue<std::string> q;
        std::mutex m;
        std::condition_variable cv;
        bool stop = false;

        void Push(std::string val) {
            std::unique_lock<std::mutex> lock(m);
            q.push(std::move(val));
            cv.notify_one();
        }

        bool Pop(std::string& val) {
            std::unique_lock<std::mutex> lock(m);
            cv.wait(lock, [this] { return !q.empty() || stop; });
            if (q.empty() && stop) return false;
            val = std::move(q.front());
            q.pop();
            return true;
        }

        void Stop() {
            std::unique_lock<std::mutex> lock(m);
            stop = true;
            cv.notify_all();
        }
    } queue_;

    std::vector<std::thread> workers_;

    // --- Worker 线程逻辑 ---
    void WorkerLoop() {
        // key: 时间戳(秒级对齐), value: {单词: 词频}
        // 使用 map 而不是 unordered_map 主要是为了调试方便（有序）
        std::map<long long, std::unordered_map<std::string, int>> time_separated_buffer;
        
        int line_count = 0;
        const int BATCH_SIZE = batch_size_; // 批处理大小

        std::string line;
        while (queue_.Pop(line)) {
            // 1. 解析时间
            long long ts = 0;
            try {
                std::string tag = ExtractTimeTag(line);
                ts = ParseTimestamp(tag);
            } catch(...) { continue; }

            // 对齐到秒 (这一步很重要，保证同一秒的数据聚在一起)
            long long bucket_ts = (ts / 1000) * 1000;

            std::size_t pos = line.find(']');
            if (pos == std::string::npos) continue;
            std::string content = line.substr(pos + 1);

            // 2. 并行分词
            std::vector<std::string> words;
            analyzer_.Split(content, words);

            // 3. 聚合到本地对应的时间桶中
            for (const auto& w : words) {
                if (w.size() > 3 && w != "\r" && w != "\n") {
                    time_separated_buffer[bucket_ts][w]++;
                }
            }
            line_count++;

            // 4. 批量提交
            if (line_count >= BATCH_SIZE) {
                // 遍历本地缓冲区的所有时间点
                for (auto& kv : time_separated_buffer) {
                    long long current_ts = kv.first;
                    auto& counts = kv.second;
                    // 调用IngestBatch
                    analyzer_.IngestBatch(counts, current_ts);
                }
                
                // 清理
                time_separated_buffer.clear();
                line_count = 0;
            }
        }

        // 5. 退出前提交剩余数据
        for (auto& kv : time_separated_buffer) {
            analyzer_.IngestBatch(kv.second, kv.first);
        }
    }

public:
    AsyncProcessor(Analyzer& analyzer, int batch_size = 10) : analyzer_(analyzer), batch_size_(batch_size) {}

    // 启动 N 个工作线程
    void Start(int num_threads = 4) {
        for (int i = 0; i < num_threads; ++i) {
            workers_.emplace_back(&AsyncProcessor::WorkerLoop, this);
        }
        std::cout << "[AsyncProcessor] Started " << num_threads << " worker threads." << std::endl;
    }

    // 接收外部输入
    void PushTask(std::string line) {
        queue_.Push(std::move(line));
    }

    // 停止并等待所有任务完成
    void StopAndWait() {
        queue_.Stop();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
        std::cout << "[AsyncProcessor] All tasks finished." << std::endl;
    }
};

/*
    以下为测试延迟时使用的代码
*/
// #pragma once
// #include "Analyzer.h"
// #include <thread>
// #include <vector>
// #include <queue>
// #include <condition_variable>
// #include <mutex>
// #include <atomic>
// #include <iostream>
// #include <chrono>
// #include <algorithm>
// #include <numeric>

// struct Task {
//     std::string data;
//     std::chrono::steady_clock::time_point entry_time; // 记录进入队列的时间
// };

// class AsyncProcessor {
// private:
//     Analyzer& analyzer_;
//     int batch_size_ = 10;
    
//     std::vector<long long> latencies_us_;
//     std::mutex latency_mutex_;

//     struct SafeQueue {
//         std::queue<Task> q;
//         std::mutex m;
//         std::condition_variable cv;
//         bool stop = false;

//         void Push(Task val) {
//             {
//                 std::unique_lock<std::mutex> lock(m);
//                 q.push(std::move(val));
//             }
//             cv.notify_one();
//         }

//         bool Pop(Task& val) {
//             std::unique_lock<std::mutex> lock(m);
//             cv.wait(lock, [this] { return !q.empty() || stop; });
//             if (q.empty() && stop) return false;
//             val = std::move(q.front());
//             q.pop();
//             return true;
//         }

//         void Stop() {
//             std::unique_lock<std::mutex> lock(m);
//             stop = true;
//             cv.notify_all();
//         }
//     } queue_;

//     std::vector<std::thread> workers_;

//     void WorkerLoop() {
//         std::map<long long, std::unordered_map<std::string, int>> time_separated_buffer;
//         int line_count = 0;
//         const int BATCH_SIZE = batch_size_;

//         std::vector<long long> local_latencies; 
//         local_latencies.reserve(20000);

//         Task task;
//         while (queue_.Pop(task)) {
//             std::string& line = task.data;

//             // 1. 解析时间
//             long long ts = 0;
//             try {
//                 std::string tag = ExtractTimeTag(line);
//                 ts = ParseTimestamp(tag);
//             } catch(...) { continue; }

//             long long bucket_ts = (ts / 1000) * 1000;
//             std::size_t pos = line.find(']');
//             if (pos == std::string::npos) continue;
//             std::string content = line.substr(pos + 1); 

//             std::vector<std::string> words;
//             analyzer_.Split(content, words);

//             for (const auto& w : words) {
//                 if (w.size() > 1) {
//                     time_separated_buffer[bucket_ts][w]++;
//                 }
//             }
//             line_count++;

//             // 计算单条处理延迟
//             auto now = std::chrono::steady_clock::now();
//             auto latency = std::chrono::duration_cast<std::chrono::microseconds>(now - task.entry_time).count();
//             local_latencies.push_back(latency);

//             if (line_count >= BATCH_SIZE) {
//                 for (auto& kv : time_separated_buffer) {
//                     analyzer_.IngestBatch(kv.second, kv.first);
//                 }
//                 time_separated_buffer.clear();
//                 line_count = 0;
//             }
//         }

//         for (auto& kv : time_separated_buffer) {
//             analyzer_.IngestBatch(kv.second, kv.first);
//         }

//         std::lock_guard<std::mutex> lock(latency_mutex_);
//         latencies_us_.insert(latencies_us_.end(), local_latencies.begin(), local_latencies.end());
//     }

// public:
//     AsyncProcessor(Analyzer& analyzer, int batch_size = 10) : analyzer_(analyzer), batch_size_(batch_size) {}

//     void Start(int num_threads = 4) {
//         latencies_us_.clear();
//         for (int i = 0; i < num_threads; ++i) {
//             workers_.emplace_back(&AsyncProcessor::WorkerLoop, this);
//         }
//         std::cout << "[AsyncProcessor] Started " << num_threads << " worker threads." << std::endl;
//     }

//     void PushTask(std::string line) {
//         Task t;
//         t.data = std::move(line);
//         t.entry_time = std::chrono::steady_clock::now(); // 记录此刻
//         queue_.Push(std::move(t));
//     }

//     void PushBatch(const std::vector<std::string>& lines) {
//         for(const auto& line : lines) {
//             PushTask(line);
//         }
//     }

//     void StopAndWait() {
//         queue_.Stop();
//         for (auto& t : workers_) {
//             if (t.joinable()) t.join();
//         }
//         std::cout << "[AsyncProcessor] All tasks finished." << std::endl;
//     }

//     // 输出延迟统计报告
//     void PrintLatencyStats() {
//         if (latencies_us_.empty()) {
//             std::cout << "[Latency] No data collected." << std::endl;
//             return;
//         }

//         std::sort(latencies_us_.begin(), latencies_us_.end());
        
//         size_t n = latencies_us_.size();
//         double sum = std::accumulate(latencies_us_.begin(), latencies_us_.end(), 0.0);
//         double avg = sum / n;
//         long long min_lat = latencies_us_.front();
//         long long max_lat = latencies_us_.back();
//         long long p50 = latencies_us_[n * 0.50];
//         long long p95 = latencies_us_[n * 0.95];
//         long long p99 = latencies_us_[n * 0.99];

//         std::cout << "\n========== Latency Report (microseconds) ==========" << std::endl;
//         std::cout << "Sample Count: " << n << std::endl;
//         std::cout << "Avg: " << avg << " us" << std::endl;
//         std::cout << "Min: " << min_lat << " us" << std::endl;
//         std::cout << "Max: " << max_lat << " us" << std::endl;
//         std::cout << "---------------------------------------------------" << std::endl;
//         std::cout << "P50 (Median): " << p50 << " us" << std::endl;
//         std::cout << "P95         : " << p95 << " us" << std::endl;
//         std::cout << "P99         : " << p99 << " us" << std::endl;
//         std::cout << "===================================================" << std::endl;
//     }
// };