#include "crow.h"
#include "Analyzer.h"
#include "AsyncProcessor.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>
#include <chrono>

// ==========================================
// 辅助函数：MIME 类型判断
// ==========================================
std::string GetMimeType(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".js"))   return "application/javascript";
    if (path.ends_with(".css"))  return "text/css";
    if (path.ends_with(".png"))  return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    if (path.ends_with(".svg"))  return "image/svg+xml";
    if (path.ends_with(".ico"))  return "image/x-icon";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".woff2")) return "font/woff2";
    if (path.ends_with(".woff"))  return "font/woff";
    if (path.ends_with(".ttf"))   return "font/ttf";
    return "application/octet-stream";
}

// ==========================================
// 辅助函数：读取并发送静态文件
// ==========================================
crow::response ServeStaticFile(const std::string& filepath) {
    // 必须以二进制模式打开，否则图片/字体会损坏
    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs.is_open()) {
        // --- 调试日志 ---
        std::cerr << "[Error] Failed to open file: " << filepath 
                  << " | Reason: " << std::strerror(errno) << std::endl;
        // ------------------
        return crow::response(404);
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();

    crow::response resp(200, oss.str());
    resp.set_header("Content-Type", GetMimeType(filepath));
    return resp;
}

int main(int argc, char* argv[]) {
    // 1. 初始化核心业务逻辑
    std::cout << "[Init] Loading dictionaries..." << std::endl;
    Analyzer analyzer("../include/dict/jieba.dict.utf8", 
        "../include/dict/hmm_model.utf8", 
        "../include/dict/user.dict.utf8", 
        "../include/dict/idf.utf8", 
        "../include/dict/stop_words.utf8"); 
    
    int batch_size = 10;
    int num_threads = 8;
    try {
        if (argc >= 2) {
            // ./app [batch_size]
            batch_size = std::stoi(argv[1]);
        }
        
        if (argc >= 3) {
            // ./app [batch_size] [num_threads]
            num_threads = std::stoi(argv[2]);
        }
    } catch (const std::exception& e) {
        std::cerr << "Parameters format error, pls use integer. Error msg: " << e.what() << std::endl;
        return 1;
    }
    
    AsyncProcessor processor(analyzer, batch_size);
    processor.Start(num_threads); // 启动8个处理线程

    // 2. 初始化 Web 服务器
    crow::SimpleApp app;

    // 定义静态文件根目录 (相对于 exe 文件)
    const std::string STATIC_ROOT = "./static"; 

    // ============================================================
    // Lambda: 统一序列化 TopK 结果为 JSON
    // ============================================================
    auto SerializeTopK = [](const std::vector<std::pair<std::string, int>>& result) {
        crow::json::wvalue json_resp;
        json_resp["data"] = crow::json::wvalue::list();
        for (size_t i = 0; i < result.size(); ++i) {
            crow::json::wvalue item;
            item["word"] = result[i].first;
            item["count"] = result[i].second;
            json_resp["data"][i] = std::move(item);
        }
        json_resp["status"] = "success";
        return json_resp;
    };

    // ============================================================
    // API 路由定义 (必须在 Catch-All 之前定义)
    // ============================================================

    // API 1: 数据输入
    CROW_ROUTE(app, "/api/ingest").methods(crow::HTTPMethod::POST)
    ([&processor](const crow::request& req){
        std::string line = req.body;
        if (line.empty()) return crow::response(400);
        processor.PushTask(line);
        return crow::response(200, "OK");
    });

    // API 2: 实时 TopK (最近10分钟)
    CROW_ROUTE(app, "/api/topk")
    ([&analyzer, &SerializeTopK](const crow::request& req){
        int k = 10;
        if (req.url_params.get("k") != nullptr) k = std::stoi(req.url_params.get("k"));
        return SerializeTopK(analyzer.GetLast10MinTopK(k));
    });

    // API 3: 全量历史 TopK
    CROW_ROUTE(app, "/api/history")
    ([&analyzer, &SerializeTopK](const crow::request& req){
        int k = 20;
        if (req.url_params.get("k") != nullptr) k = std::stoi(req.url_params.get("k"));
        return SerializeTopK(analyzer.GetTopK(k));
    });

    // API 4: 自定义时间段查询
    CROW_ROUTE(app, "/api/range")
    ([&analyzer, &SerializeTopK](const crow::request& req){
        long long start_ts = 0;
        long long end_ts = 0;
        int k = 10;
        if (req.url_params.get("start") != nullptr) start_ts = std::stoll(req.url_params.get("start"));
        if (req.url_params.get("end") != nullptr) end_ts = std::stoll(req.url_params.get("end"));
        if (req.url_params.get("k") != nullptr) k = std::stoi(req.url_params.get("k"));
        
        if (end_ts == 0) {
            end_ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
        return SerializeTopK(analyzer.GetTopKInTimeRange(start_ts, end_ts, k));
    });

    // API 5: 趋势分析 (Trending)
    CROW_ROUTE(app, "/api/trending")
    ([&analyzer](const crow::request& req){
        int k = 3; 
        if (req.url_params.get("k") != nullptr) k = std::stoi(req.url_params.get("k"));
        int threshold = 5; 
        if (req.url_params.get("threshold") != nullptr) threshold = std::stoi(req.url_params.get("threshold"));

        std::vector<TrendItem> trends = analyzer.GetTrending(k, threshold);

        crow::json::wvalue json_resp;
        json_resp["data"] = crow::json::wvalue::list();
        for (size_t i = 0; i < trends.size(); ++i) {
            crow::json::wvalue item;
            item["word"] = trends[i].word;
            item["slope"] = trends[i].slope;
            item["count"] = trends[i].total_count;
            
            if (trends[i].slope > 1.0) item["tag"] = "rising"; 
            else if (trends[i].slope < -1.0) item["tag"] = "falling";
            else item["tag"] = "stable";

            json_resp["data"][i] = std::move(item);
        }
        json_resp["status"] = "success";
        json_resp["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        return json_resp;
    });

    // ============================================================
    // 静态资源路由 (Catch-All)
    // ============================================================
    CROW_CATCHALL_ROUTE(app)
    ([STATIC_ROOT](const crow::request& req){
        std::string path = req.url;
        
        // 去掉 URL 参数（如果有 ?k=v 这种）
        size_t query_pos = path.find('?');
        if (query_pos != std::string::npos) {
            path = path.substr(0, query_pos);
        }

        // 1. 默认首页
        if (path == "/" || path == "") {
            return ServeStaticFile(STATIC_ROOT + "/index.html");
        }

        // 2. 构造物理路径
        std::string file_path = STATIC_ROOT + path;

        // --- 调试日志：打印到底在找哪个文件，以及当前工作目录在哪里 ---
        std::cout << "[Debug] Request: " << path << " -> Looking at: " << std::filesystem::absolute(file_path) << std::endl;

        // 安全检查
        if (path.find("..") != std::string::npos) return crow::response(403);

        // 3. 尝试查找物理文件
        if (std::filesystem::exists(file_path) && std::filesystem::is_regular_file(file_path)) {
            std::cout << "[Debug] File exists, trying to serve..." << std::endl;
            return ServeStaticFile(file_path);
        }

        // 4. SPA Fallback
        // 如果请求的是静态资源（assets, js, css, png...），找不到就是真的 404，不要返回 index.html
        // Vue/Vite 打包默认会把资源放在 assets 目录下，或者通过后缀判断
        bool is_static_asset = false;
        if (path.starts_with("/assets/") || 
            path.ends_with(".js") || 
            path.ends_with(".css") || 
            path.ends_with(".png") || 
            path.ends_with(".ico")) {
            is_static_asset = true;
        }

        if (is_static_asset) {
            std::cout << "[404] Missing asset: " << file_path << std::endl;
            return crow::response(404); // 明确告知浏览器文件不存在
        }

        // 只有非资源的路径（比如 /user/login 这种页面路由）才返回 index.html
        return ServeStaticFile(STATIC_ROOT + "/index.html");
    });

    // 3. 启动服务器
    std::cout << "[Init] Server starting at http://localhost:18080" << std::endl;
    std::cout << "[Init] Serving static files from: " << std::filesystem::absolute(STATIC_ROOT) << std::endl;
    
    app.port(18080).multithreaded().run();

    // 4. 退出清理
    processor.StopAndWait();
    return 0;
}

// #include <iostream>
// #include <vector>
// #include <string>
// #include <chrono>
// #include <random>
// #include "Analyzer.h"
// #include "AsyncProcessor.h"

// // --- 生成数据 ---
// std::vector<std::string> GenerateTestData(int count) {
//     std::vector<std::string> data;
//     data.reserve(count);
//     std::vector<std::string> dict = {"Test", "Latency", "Performance", "Jieba", "C++", "Vtune", "Intel"};
//     std::mt19937 rng(42);
//     std::uniform_int_distribution<int> idx_dist(0, dict.size() - 1);
    
//     for (int i = 0; i < count; ++i) {
//         std::string line = "[12:00:01] ";
//         for (int j = 0; j < 5; ++j) line += dict[idx_dist(rng)] + " ";
//         data.push_back(line);
//     }
//     return data;
// }

// int main(int argc, char* argv[]) {
//     // 1. 设置参数
//     int batch_size = 500;
//     int num_threads = 8;
//     int total_requests = 100000;
    
//     if (argc >= 2) batch_size = std::stoi(argv[1]);
//     if (argc >= 3) num_threads = std::stoi(argv[2]);

//     auto test_data = GenerateTestData(total_requests);

//     // 2. 初始化
//     // Analyzer analyzer("dict/jieba.dict.utf8", "dict/hmm_model.utf8", "dict/user.dict.utf8", "dict/idf.utf8", "dict/stop_words.utf8");
//     Analyzer analyzer("../include/dict/jieba.dict.utf8", 
//         "../include/dict/hmm_model.utf8", 
//         "../include/dict/user.dict.utf8", 
//         "../include/dict/idf.utf8", 
//         "../include/dict/stop_words.utf8"); 
//     AsyncProcessor processor(analyzer, batch_size);

//     std::cout << "------------------------------------------------" << std::endl;
//     std::cout << "[Latency Test] Start..." << std::endl;
//     std::cout << "  - Requests: " << total_requests << std::endl;
//     std::cout << "  - Threads:  " << num_threads << std::endl;
//     std::cout << "------------------------------------------------" << std::endl;

//     processor.Start(num_threads);

//     auto t_start = std::chrono::high_resolution_clock::now();

//     // 3. 生产数据
//     for (const auto& line : test_data) {
//         // 增加sleep来降低压力
//         // using namespace std::chrono_literals;
//         // std::this_thread::sleep_for(0.2ms);
//         processor.PushTask(line);
//     }

//     processor.StopAndWait();
    
//     auto t_end = std::chrono::high_resolution_clock::now();
//     auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

//     // 4. 输出结果
//     std::cout << "\n[Summary]" << std::endl;
//     std::cout << "Total Wall Time: " << duration << " ms" << std::endl;
//     std::cout << "Throughput:      " << (total_requests * 1000.0 / duration) << " QPS" << std::endl;

//     // 5. 打印延迟分布
//     processor.PrintLatencyStats();

//     return 0;
// }

// #include <iostream>
// #include <vector>
// #include <string>
// #include <chrono>
// #include <random>
// #include <iomanip>
// #include <fstream>
// #include <unistd.h>
// #include <sys/resource.h> // Linux 下用于获取内存占用
// #include "Analyzer.h"
// #include "AsyncProcessor.h"

// // --- 配置区域 ---
// const int TOTAL_LINES = 2000000; // 增加数据量，保证运行时间足够 VTune 采样
// const int WARMUP_LINES = 10000;  // 预热数据量

// // --- 获取当前内存占用 (KB) ---
// long getMemoryUsage() {
//     struct rusage usage;
//     if (0 == getrusage(RUSAGE_SELF, &usage)) {
//         return usage.ru_maxrss; // Linux下单位是KB
//     }
//     return 0;
// }

// // 数据生成
// std::vector<std::string> GenerateTestData(int count) {
//     std::cout << "[System] Generating " << count << " lines of test data..." << std::endl;
//     std::vector<std::string> data;
//     data.reserve(count);

//     // 稍微扩充词典，模拟更多样的情况
//     std::vector<std::string> dict = {
//         "Python", "C++", "Java", "Golang", "Rust", "I", "Love", "Coding",
//         "Algorithm", "DataStructure", "HighPerformance", "Concurrency", "Test",
//         "Beijing", "Shanghai", "Tokyo", "NewYork", "Weather", "Food", "Good",
//         "Microservice", "Distributed", "System", "Linux", "Kernel", "Network"
//     };

//     std::mt19937 rng(42); 
//     std::uniform_int_distribution<int> word_dist(0, dict.size() - 1);
//     std::uniform_int_distribution<int> len_dist(3, 15); // 长度波动
//     std::uniform_int_distribution<int> time_dist(0, 59);

//     for (int i = 0; i < count; ++i) {
//         std::string line = "[12:00:";
//         int sec = time_dist(rng);
//         if (sec < 10) line += "0";
//         line += std::to_string(sec) + "] ";
        
//         int len = len_dist(rng);
//         for (int j = 0; j < len; ++j) {
//             line += dict[word_dist(rng)];
//             if (j < len - 1) line += " "; 
//         }
//         data.push_back(std::move(line));
//     }
//     return data;
// }

// int main(int argc, char* argv[]) {
//     // 1. 参数解析
//     int batch_size = 200;
//     int num_threads = 8;
//     if (argc >= 2) batch_size = std::stoi(argv[1]);
//     if (argc >= 3) num_threads = std::stoi(argv[2]);

//     // 2. 准备数据 (分为预热集和测试集)
//     auto warmup_data = GenerateTestData(WARMUP_LINES);
//     auto test_data = GenerateTestData(TOTAL_LINES);
    
//     long mem_before = getMemoryUsage();
//     std::cout << "[System] Base Memory Usage: " << mem_before / 1024.0 << " MB" << std::endl;

//     // 3. 初始化分析器
//     std::cout << "[System] Initializing Analyzer..." << std::endl;
//     Analyzer analyzer("../include/dict/jieba.dict.utf8", 
//         "../include/dict/hmm_model.utf8", 
//         "../include/dict/user.dict.utf8", 
//         "../include/dict/idf.utf8", 
//         "../include/dict/stop_words.utf8"); 
    
//     // 4. 初始化处理器
//     AsyncProcessor processor(analyzer, batch_size);
//     processor.Start(num_threads);

//     // --- A. 预热阶段 (Warm-up) ---
//     // 目的：填充 CPU Cache，触发缺页中断，让线程池进入状态
//     std::cout << "[Benchmark] Warming up..." << std::endl;
//     for (const auto& line : warmup_data) {
//         processor.PushTask(line);
//     }
//     // 等待预热完成，但不停止线程池
//     // 这里假设 StopAndWait 会销毁线程，如果是这样，预热可能需要重构 Processor。
//     // 我们假设这里的预热只是让各个 Cache 变热
    
//     // --- B. 正式测试阶段 ---
//     std::cout << "------------------------------------------------" << std::endl;
//     std::cout << "[Benchmark] Starting Measurement..." << std::endl;
//     std::cout << "  - Total Requests: " << TOTAL_LINES << std::endl;
//     std::cout << "  - Threads:        " << num_threads << std::endl;
//     std::cout << "  - Batch Size:     " << batch_size << std::endl;
//     std::cout << "------------------------------------------------" << std::endl;

//     auto t_start = std::chrono::high_resolution_clock::now();

//     // 生产
//     for (const auto& line : test_data) {
//         processor.PushTask(line);
//     }

//     // 等待全部处理完毕
//     processor.StopAndWait();

//     auto t_end = std::chrono::high_resolution_clock::now();
//     // --- 结束 ---

//     // 5. 结果计算
//     auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
//     double seconds = duration_ms / 1000.0;
//     double qps = TOTAL_LINES / seconds;
//     long mem_peak = getMemoryUsage();

//     std::cout << "\n[Benchmark] Finished!" << std::endl;
//     std::cout << "------------------------------------------------" << std::endl;
//     std::cout << "  - Time Cost:      " << duration_ms << " ms" << std::endl;
//     std::cout << "  - Throughput:     " << std::fixed << std::setprecision(2) << qps << " lines/sec" << std::endl;
//     std::cout << "  - Peak Memory:    " << mem_peak / 1024.0 << " MB" << std::endl;
//     std::cout << "  - Memory Growth:  " << (mem_peak - mem_before) / 1024.0 << " MB" << std::endl;
//     std::cout << "------------------------------------------------" << std::endl;

//     // 6. 简单的正确性校验 (防止编译器优化掉整个逻辑)
//     auto result = analyzer.GetTopK(1);
//     std::cout << "[Check] Top 1 Word: " << (result.empty() ? "None" : result[0].first) << std::endl;

//     return 0;
// }

// #include <iostream>
// #include <vector>
// #include <string>
// #include <chrono>
// #include <random>
// #include <iomanip>
// #include "Analyzer.h"
// #include "AsyncProcessor.h"

// // --- 配置区域 ---
// const int TOTAL_LINES = 1000000; // 测试总数据量：100万条 (根据你的内存大小调整)

// // --- 辅助函数：生成模拟数据 ---
// // 为了测试准确，我们需要先生成数据，避免把"生成字符串的时间"算进"处理时间"里
// std::vector<std::string> GenerateTestData(int count) {
//     std::cout << "[System] Generating " << count << " lines of test data..." << std::endl;
//     std::vector<std::string> data;
//     data.reserve(count);

//     std::vector<std::string> dict = {
//         "Python", "C++", "Java", "Golang", "Rust", "I", "Love", "Coding",
//         "Algorithm", "DataStructure", "HighPerformance", "Concurrency", "Test",
//         "Beijing", "Shanghai", "Tokyo", "NewYork", "Weather", "Food", "Good"
//     };

//     std::mt19937 rng(42); // 固定种子保证每次结果一致
//     std::uniform_int_distribution<int> word_dist(0, dict.size() - 1);
//     std::uniform_int_distribution<int> len_dist(3, 10); // 句子长度 3-10 个词
//     std::uniform_int_distribution<int> time_dist(0, 59);

//     for (int i = 0; i < count; ++i) {
//         // 构造时间戳 [12:00:xx]
//         std::string line = "[12:00:";
//         int sec = time_dist(rng);
//         if (sec < 10) line += "0";
//         line += std::to_string(sec) + "] ";

//         // 构造句子
//         int len = len_dist(rng);
//         for (int j = 0; j < len; ++j) {
//             line += dict[word_dist(rng)];
//             if (j < len - 1) line += " "; // 既然用jieba，加不加空格其实它都能分，但模拟自然语言
//         }
//         data.push_back(std::move(line));
//     }
    
//     std::cout << "[System] Data generation complete." << std::endl;
//     return data;
// }

// int main(int argc, char* argv[]) {
//     // 1. 准备数据 (不计入处理耗时)
//     auto test_data = GenerateTestData(TOTAL_LINES);

//     int batch_size = 200;
//     int num_threads = 8;
//     try {
//         if (argc >= 2) {
//             // ./app [batch_size]
//             batch_size = std::stoi(argv[1]);
//         }
        
//         if (argc >= 3) {
//             // ./app [batch_size] [num_threads]
//             num_threads = std::stoi(argv[2]);
//         }
//     } catch (const std::exception& e) {
//         std::cerr << "Parameters format error, pls use integer. Error msg: " << e.what() << std::endl;
//         return 1;
//     }

//     // 2. 初始化分析器 (加载字典耗时较长，不计入核心吞吐量测试，但为了真实性可以看一眼)
//     std::cout << "[System] Initializing Analyzer (Loading Dicts)..." << std::endl;
//     auto t_init_start = std::chrono::high_resolution_clock::now();
    
//     Analyzer analyzer("../include/dict/jieba.dict.utf8", 
//         "../include/dict/hmm_model.utf8", 
//         "../include/dict/user.dict.utf8", 
//         "../include/dict/idf.utf8", 
//         "../include/dict/stop_words.utf8"); 
    
//     auto t_init_end = std::chrono::high_resolution_clock::now();
//     std::cout << "[System] Analyzer initialized in " 
//               << std::chrono::duration_cast<std::chrono::milliseconds>(t_init_end - t_init_start).count() 
//               << " ms." << std::endl;

//     // 3. 初始化多线程处理器
//     AsyncProcessor processor(analyzer, batch_size);

//     std::cout << "------------------------------------------------" << std::endl;
//     std::cout << "[Benchmark] Starting Benchmark..." << std::endl;
//     std::cout << "  - Total Requests: " << TOTAL_LINES << std::endl;
//     std::cout << "  - Threads:        " << num_threads << std::endl;
//     std::cout << "------------------------------------------------" << std::endl;

//     // --- 计时开始 ---
//     auto t_start = std::chrono::high_resolution_clock::now();

//     // A. 启动 Worker 线程
//     processor.Start(num_threads);

//     // B. 推送任务 (模拟主线程接收数据)
//     // 这里的 PushTask 只是把 string 指针塞入队列，非常快
//     for (const auto& line : test_data) {
//         processor.PushTask(line);
//     }

//     // C. 等待所有任务处理完毕
//     // StopAndWait 会等待队列清空且所有 Worker 线程处理完手头的 Batch
//     processor.StopAndWait();

//     // --- 计时结束 ---
//     auto t_end = std::chrono::high_resolution_clock::now();

//     // 4. 计算结果
//     auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
//     double seconds = duration_ms / 1000.0;
//     double qps = TOTAL_LINES / seconds; // 每秒处理条数

//     std::cout << "\n[Benchmark] Finished!" << std::endl;
//     std::cout << "------------------------------------------------" << std::endl;
//     std::cout << "  - Total Time:   " << duration_ms << " ms" << std::endl;
//     std::cout << "  - Throughput:   " << std::fixed << std::setprecision(2) << qps << " lines/second" << std::endl;
//     std::cout << "------------------------------------------------" << std::endl;

//     // 5. 验证结果 (确保并没有被编译器优化掉，且逻辑正确)
//     std::cout << "\n[Sanity Check] Querying Top 5 Words:" << std::endl;
//     auto result = analyzer.GetTopK(5);
//     for (const auto& p : result) {
//         std::cout << "  " << p.first << ": " << p.second << std::endl;
//     }

//     std::cout << "\n[Sanity Check] Querying 10-Min Window Top 5:" << std::endl;
//     auto win_result = analyzer.GetLast10MinTopK(5);
//     for (const auto& p : win_result) {
//         std::cout << "  " << p.first << ": " << p.second << std::endl;
//     }

//     analyzer.DebugPrint();

//     return 0;
// }

// #include <iostream>
// #include <vector>
// #include <string>
// #include <thread>
// #include <chrono>
// #include <cassert>
// #include <map>
// #include <sstream>
// #include <iomanip>
// #include <algorithm>
// #include <random>
// #include "Analyzer.h"
// #include "AsyncProcessor.h"

// // --- 测试辅助工具 ---

// // 1. 断言函数
// template <typename T>
// void AssertEqual(T actual, T expected, const std::string& message) {
//     if (actual == expected) {
//         std::cout << "[PASS] " << message << " (Value: " << actual << ")" << std::endl;
//     } else {
//         std::cerr << "[FAIL] " << message << " (Expected: " << expected << ", Actual: " << actual << ")" << std::endl;
//         exit(1);
//     }
// }

// // 2. 检查某个词是否在 TopK 列表中，并返回其计数
// int GetCountFromList(const std::vector<std::pair<std::string, int>>& list, const std::string& target) {
//     for (const auto& p : list) {
//         if (p.first == target) return p.second;
//     }
//     return 0; // Not found
// }

// // 3. 检查某个词是否在 Trend 列表中
// bool IsInTrendList(const std::vector<TrendItem>& list, const std::string& target) {
//     for (const auto& item : list) {
//         if (item.word == target) return true; 
//     }
//     return false;
// }

// // 4. 时间生成器 (毫秒)
// struct TimePair {
//     std::string log_str; 
//     long long ts;        
// };

// TimePair GetTimePair(int offset_seconds) {
//     auto now = std::chrono::system_clock::now();
//     now += std::chrono::seconds(offset_seconds);
//     auto time_t_val = std::chrono::system_clock::to_time_t(now);
//     std::tm bt = *std::localtime(&time_t_val);
    
//     std::stringstream ss;
//     ss << "[" << std::setfill('0') << std::setw(2) << bt.tm_hour << ":"
//        << std::setfill('0') << std::setw(2) << bt.tm_min << ":"
//        << std::setfill('0') << std::setw(2) << bt.tm_sec << "]";
    
//     // 手动计算毫秒数
//     long long ts = (bt.tm_hour * 3600LL + bt.tm_min * 60LL + bt.tm_sec) * 1000LL;

//     return {ss.str(), ts};
// }

// // --- 主测试逻辑 ---

// int main() {
//     std::cout << "===============================================" << std::endl;
//     std::cout << "      Randomized Interface Correctness Test" << std::endl;
//     std::cout << "===============================================" << std::endl;

//     // 1. 初始化
//     Analyzer analyzer("../include/dict/jieba.dict.utf8", 
//                       "../include/dict/hmm_model.utf8", 
//                       "../include/dict/user.dict.utf8", 
//                       "../include/dict/idf.utf8", 
//                       "../include/dict/stop_words.utf8");
    
//     AsyncProcessor processor(analyzer, 1);
//     processor.Start(2);

//     // 2. 准备随机数生成器
//     std::random_device rd;
//     std::mt19937 gen(rd());
    
//     // 定义不同强度的随机分布
//     std::uniform_int_distribution<> dist_small(2, 5);   // 低频词 (2-5次)
//     std::uniform_int_distribution<> dist_mid(6, 15);    // 中频词 (6-15次)
//     std::uniform_int_distribution<> dist_burst(20, 40); // 爆发词 (20-40次)

//     // 3. 准备时间点
//     auto t1 = GetTimePair(-3600); // 1小时前
//     auto t2 = GetTimePair(-1800); // 30分钟前
//     auto t3 = GetTimePair(0);     // 现在

//     std::cout << "[Setup] Time Points (ms): T1=" << t1.ts << ", T2=" << t2.ts << ", T3=" << t3.ts << std::endl;

//     // 4. 生成随机计数并构造数据
//     // -------------------------------------------------
//     // T1: Jurassic (Mid)
//     int cnt_jurassic = dist_mid(gen);
    
//     // T2: Medieval (Mid)
//     int cnt_medieval = dist_mid(gen);

//     // T3: Cyberpunk (Mid)
//     int cnt_cyberpunk = dist_mid(gen);

//     // T3: ViralNews (Burst) - 确保它比其他的都多，以便测试 Trending
//     int cnt_viral = dist_burst(gen);

//     // Always: 在 T1, T2, T3 各出现少量次数
//     int cnt_always_t1 = dist_small(gen);
//     int cnt_always_t2 = dist_small(gen);
//     int cnt_always_t3 = dist_small(gen);
//     int cnt_always_total = cnt_always_t1 + cnt_always_t2 + cnt_always_t3;

//     std::cout << "[Setup] Generated Random Counts:" << std::endl;
//     std::cout << "  Jurassic (T1):  " << cnt_jurassic << std::endl;
//     std::cout << "  Medieval (T2):  " << cnt_medieval << std::endl;
//     std::cout << "  Cyberpunk (T3): " << cnt_cyberpunk << std::endl;
//     std::cout << "  ViralNews (T3): " << cnt_viral << std::endl;
//     std::cout << "  Always (Total): " << cnt_always_total << " (T1:" << cnt_always_t1 << ", T2:" << cnt_always_t2 << ", T3:" << cnt_always_t3 << ")" << std::endl;

//     std::vector<std::string> lines;

//     // 构造 T1 数据
//     for(int i=0; i<cnt_jurassic; ++i)  lines.push_back(t1.log_str + " Jurassic");
//     for(int i=0; i<cnt_always_t1; ++i) lines.push_back(t1.log_str + " Always");

//     // 构造 T2 数据
//     for(int i=0; i<cnt_medieval; ++i)  lines.push_back(t2.log_str + " Medieval");
//     for(int i=0; i<cnt_always_t2; ++i) lines.push_back(t2.log_str + " Always");

//     // 构造 T3 数据
//     for(int i=0; i<cnt_cyberpunk; ++i) lines.push_back(t3.log_str + " Cyberpunk");
//     // 1. 先塞一半在 T3
//     int half_viral = cnt_viral / 2;
//     for(int i=0; i<half_viral; ++i)     lines.push_back(t3.log_str + " ViralNews");
//     // 2. 再塞一半在 T3 + 5秒
//     TimePair t3_later = GetTimePair(5); // 5秒后
//     for(int i=half_viral; i<cnt_viral; ++i) lines.push_back(t3_later.log_str + " ViralNews");
//     for(int i=0; i<cnt_always_t3; ++i) lines.push_back(t3.log_str + " Always");

//     // 打乱顺序，模拟真实数据流
//     std::shuffle(lines.begin(), lines.end(), gen);

//     std::cout << "[Action] Pushing " << lines.size() << " lines of data..." << std::endl;
//     for (const auto& line : lines) {
//         processor.PushTask(line);
//     }

//     processor.StopAndWait();
//     std::cout << "[System] Processing complete. Verifying...\n" << std::endl;

//     // ---------------------------------------------------------
//     // 测试 1: GetTopK (全量统计)
//     // ---------------------------------------------------------
//     std::cout << "--- Test 1: GetTopK (Global) ---" << std::endl;
//     auto res_all = analyzer.GetTopK(50); // 取多一点防止漏掉
    
//     AssertEqual(GetCountFromList(res_all, "ViralNews"), cnt_viral, "Global count of ViralNews");
//     AssertEqual(GetCountFromList(res_all, "Cyberpunk"), cnt_cyberpunk,  "Global count of Cyberpunk");
//     AssertEqual(GetCountFromList(res_all, "Medieval"),  cnt_medieval,   "Global count of Medieval");
//     AssertEqual(GetCountFromList(res_all, "Jurassic"),  cnt_jurassic,   "Global count of Jurassic");
//     AssertEqual(GetCountFromList(res_all, "Always"),    cnt_always_total, "Global count of Always");

//     // ---------------------------------------------------------
//     // 测试 2: GetTopKInTimeRange (指定时间段)
//     // ---------------------------------------------------------
//     std::cout << "--- Test 2: GetTopKInTimeRange ---" << std::endl;

//     // 修正：范围计算使用毫秒
//     long long range_start = t2.ts - 10000; 
//     long long range_end   = t2.ts + 10000;
//     auto res_range = analyzer.GetTopKInTimeRange(range_start, range_end, 20);

//     // T2 时段应该只有 Medieval 和 Always(T2部分)
//     AssertEqual(GetCountFromList(res_range, "Medieval"),  cnt_medieval, "Range(Mid) count of Medieval");
//     AssertEqual(GetCountFromList(res_range, "Always"),    cnt_always_t2, "Range(Mid) count of Always");
//     AssertEqual(GetCountFromList(res_range, "Jurassic"),  0, "Range(Mid) should exclude Jurassic");
//     AssertEqual(GetCountFromList(res_range, "Cyberpunk"), 0, "Range(Mid) should exclude Cyberpunk");

//     // ---------------------------------------------------------
//     // 测试 3: GetLast10MinTopK (最近10分钟窗口)
//     // ---------------------------------------------------------
//     std::cout << "--- Test 3: GetLast10MinTopK ---" << std::endl;
//     // T3 时段的数据
//     auto res_window = analyzer.GetLast10MinTopK(20);

//     AssertEqual(GetCountFromList(res_window, "ViralNews"), cnt_viral, "Window count of ViralNews");
//     AssertEqual(GetCountFromList(res_window, "Cyberpunk"), cnt_cyberpunk,  "Window count of Cyberpunk");
//     AssertEqual(GetCountFromList(res_window, "Always"),    cnt_always_t3,  "Window count of Always");
//     AssertEqual(GetCountFromList(res_window, "Medieval"),  0,  "Window should exclude Medieval");

//     // ---------------------------------------------------------
//     // 测试 4: GetTrending (突发/热门趋势)
//     // ---------------------------------------------------------
//     std::cout << "--- Test 4: GetTrending ---" << std::endl;
    
//     // 我们生成的 ViralNews 是 Burst 类型 (20-40次)，远大于其他干扰词
//     // 设定阈值为 15，确保只有 ViralNews 稳过 (Cyberpunk 是 6-15，可能过也可能不过，取决于随机数)
//     auto res_trend = analyzer.GetTrending(10, 16); 

//     bool has_viral = IsInTrendList(res_trend, "ViralNews");
    
//     if (has_viral) {
//         std::cout << "[PASS] 'ViralNews' identified as Trending." << std::endl;
//     } else {
//         std::cerr << "[FAIL] 'ViralNews' missing from Trending list! (Generated count: " << cnt_viral << ")" << std::endl;
//         exit(1);
//     }

//     std::cout << ">> Trending Debug Output:" << std::endl;
//     for(const auto& t : res_trend) {
//         std::cout << "   Trend Item: " << t.word << " (Score/Count: " << t.total_count << ")" << std::endl;
//     }

//     std::cout << "\n========================================" << std::endl;
//     std::cout << "      ALL RANDOMIZED TESTS PASSED" << std::endl;
//     std::cout << "========================================" << std::endl;

//     return 0;
// }
