#pragma once
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <set>
#include <shared_mutex>
#include <mutex>
#include "Utils.h"
#include "cppjieba/Jieba.hpp"
#include <iostream>
#include <algorithm>

using ll = long long;

struct StreamItem {
    long long timestamp;
    std::string word;
    int count;
};

// 桶：存储某一秒（或某时间段）内的统计
struct TimeBucket {
    long long bucket_start_time; // 比如第 8000ms
    std::unordered_map<std::string, int> word_counts;
};

struct TrendItem {
    std::string word;
    double slope;      // 斜率 (增长速率)
    int total_count;   // 当前窗口内总词频
};

class Analyzer {
private:
    // 1. 核心 Jieba 组件 (初始化很慢，只初始化一次)
    cppjieba::Jieba jieba_;

    // 2. 数据结构
    std::deque<TimeBucket> history_buckets_; // 所有的历史记录分桶
    std::unordered_map<std::string, int> global_counts_; // 全局总词频 (O(1)查询)
    
    // 3. 实时 Top-K 排序
    // pair<词频, 单词>。set 会自动排序。
    // 注意：set 默认从小到大排，我们取 TopK 时需要反向遍历 (rbegin)。
    std::set<std::pair<int, std::string>> ranking_set_; 

    // 4. 10min滑动窗口
    const ll WINDOW_DURATION_MS = 10 * 60 * 1000 + 1000;
    std::unordered_map<std::string, int> window_counts_;
    // std::set<std::pair<int, std::string>> window_ranking_;
    std::size_t window_start_index_ = 0;    //窗口在历史桶的起始下标

    // 5. 线程锁
    // mutable 允许在 const 函数 (如 get_top_k) 中被上锁
    mutable std::shared_mutex mutex_; 

    // 6. 工具函数：更新set排名用
    void UpdateRankingSet(std::set<std::pair<int, std::string>>& rank_set, 
        const std::string& word, int old_count, int new_count);

public:
    // 构造函数
    Analyzer(const std::string& dict_path, const std::string& hmm_path, 
             const std::string& user_dict_path, const std::string& idf_path, 
             const std::string& stop_word_path);

    // 写接口[单线程]（已被弃用，项目中未使用）
    void Ingest(const std::string& line);

    /*
        多线程写接口
    */
    void Split(const std::string& sentence, std::vector<std::string>& words) const; // 暴露无锁分词
    void IngestBatch(const std::unordered_map<std::string, int>& local_counts, long long timestamp);    //写入统计好的数据（批量防止排队）

    // 查询
    std::vector<std::pair<std::string, int>> GetTopK(int k);    // 全量查询
    std::vector<std::pair<std::string, int>> GetTopKInTimeRange(long long start_ts, long long end_ts, int k); // 任意时间段
    std::vector<std::pair<std::string, int>> GetLast10MinTopK(int k); // 10分钟窗口
    std::vector<TrendItem> GetTrending(int k, int min_threshold); // 当前趋势查询
    
    // 调试用：打印当前状态
    void DebugPrint();
};