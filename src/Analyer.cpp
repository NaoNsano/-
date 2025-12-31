#include "Analyzer.h"

/*
    构造函数，构造一次jieba对象
*/
Analyzer::Analyzer
(const std::string& dict_path, 
    const std::string& hmm_path, 
    const std::string& user_dict_path, 
    const std::string& idf_path, 
    const std::string& stop_word_path):
jieba_(dict_path, hmm_path, user_dict_path, idf_path, stop_word_path){
    
}


/*  注意：这个函数已被弃用，没有被实际使用，所以没有写入文档内，其用于在全局内插入
    1. 调用Utils解析时间戳，解析实际内容
    2. 调用jieba分词（瓶颈）
    3. 上写锁
    4. 过滤
    5. 更新计数
    6. 更新桶
*/
void Analyzer::Ingest(const std::string& line){
    // 1. 解析时间戳&内容
    long long timestamp = 0;
    try {
        timestamp = ParseTimestamp(ExtractTimeTag(line)); 
    } catch (...) {
        return; // 格式错误直接丢弃
    }
    std::size_t spilt_pos = line.find(']');
    if(spilt_pos + 1 >= line.size())    return;
    std::string content = line.substr(spilt_pos + 1);

    // 2. 分词（无锁）
    std::vector<std::string> words;
    jieba_.Cut(content, words, true);

    // 3. 写锁，分完词要写入了
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // 4. 维护时间桶 (假设按秒分桶)
    
    // 将毫秒时间戳对齐到秒 (例如 8500ms -> 8000ms)
    // 这样 8.1秒 和 8.9秒 的弹幕都会进同一个桶
    long long bucket_time = (timestamp / 1000) * 1000; 

    // 情况A: 桶是空的，或者 新时间比最后一个桶的时间还要晚 -> 开新桶
    if (history_buckets_.empty() || bucket_time > history_buckets_.back().bucket_start_time) {
        TimeBucket new_bucket;
        new_bucket.bucket_start_time = bucket_time;
        history_buckets_.push_back(std::move(new_bucket));
    }
    
    // 情况B: 乱序数据/迟到数据 (比如现在已经是 10秒，突然来个 5秒的数据)
    // 暂时不处理乱序
    TimeBucket& target_bucket = history_buckets_.back();

    // 5. 更新全局 & 桶内数据
    for (const auto& w : words) {
        // 过滤规则：字节数<=1 (过滤英文标点) 或 换行符
        if (w.size() <= 3 || w == "\r" || w == "\n") continue;

        auto map_it = global_counts_.find(w);
        int old_count = 0;
        
        if (map_it != global_counts_.end()) {
            old_count = map_it->second;
            // 只有旧词频 > 0 才需要从 Set 里删
            auto set_it = ranking_set_.find({old_count, w});
            if (set_it != ranking_set_.end()) {
                ranking_set_.erase(set_it);
            }
        }

        int new_count = old_count + 1;
        
        // 更新 Map (如果 map_it 存在直接修改，不存在则插入)
        if (map_it != global_counts_.end()) {
            map_it->second = new_count;
        } else {
            global_counts_[w] = new_count;
        }

        ranking_set_.insert({new_count, w});

        // 更新当前时间桶
        target_bucket.word_counts[w]++;
    }
}


/*
    从开始到当前所有的topk查询，从多到少
*/
std::vector<std::pair<std::string, int>> Analyzer::GetTopK(int k) {
    std::shared_lock<std::shared_mutex> lock(mutex_); 
    
    std::vector<std::pair<std::string, int>> ans;
    if (ranking_set_.empty()) return ans; // 返回空

    auto it = ranking_set_.rbegin();
    for (int i = 0; i < k && it != ranking_set_.rend(); ++i, ++it) {
        ans.push_back(std::make_pair(it->second, it->first));
    }
    return ans;
}

/*
    更新set的辅助函数
*/
void Analyzer::UpdateRankingSet(std::set<std::pair<int, std::string>>& rank_set, 
                                const std::string& word, int old_count, int new_count) {
    if (old_count > 0) {
        auto it = rank_set.find({old_count, word});
        if (it != rank_set.end()) rank_set.erase(it);
    }
    if (new_count > 0) {
        rank_set.insert({new_count, word});
    }
}

/*
    线程安全的分词接口
*/
void Analyzer::Split(const std::string& sentence, std::vector<std::string>& words) const {
    // std::cout << "Debug from Spilt: curr sentencev" << sentence << std::endl;
    jieba_.Cut(sentence, words, true);
}

/*
    批量写入和更新
*/
void Analyzer::IngestBatch(const std::unordered_map<std::string, int>& local_counts, long long timestamp) {
    if (local_counts.empty()) return;

    // 1. 加写锁
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // 2. 计算当前批次的时间戳 (对齐到秒)
    long long bucket_time = (timestamp / 1000) * 1000;

    // 步骤 A: 找到或创建正确的时间桶 (Target Bucket)
    TimeBucket* target_bucket_ptr = nullptr;

    // 情况 1: 历史为空，或者 新时间比最后一个桶还晚 (最常见情况: 顺序到达)
    if (history_buckets_.empty() || bucket_time > history_buckets_.back().bucket_start_time) {
        TimeBucket new_bucket;
        new_bucket.bucket_start_time = bucket_time;
        history_buckets_.push_back(std::move(new_bucket));
        target_bucket_ptr = &history_buckets_.back();
    } 
    // 情况 2: 乱序数据 (Late Arrival)
    else {
        // 二分查找第一个 >= bucket_time 的位置
        auto it = std::lower_bound(history_buckets_.begin(), history_buckets_.end(), bucket_time, 
            [](const TimeBucket& bucket, long long val) {
                return bucket.bucket_start_time < val;
            });

        // 2.1 找到了精确匹配的时间桶 -> 复用
        if (it != history_buckets_.end() && it->bucket_start_time == bucket_time) {
            target_bucket_ptr = &(*it);
        } 
        // 2.2 没找到精确匹配 (是时间空洞) -> 插入新桶
        else {
            TimeBucket new_bucket;
            new_bucket.bucket_start_time = bucket_time;
            
            // 插入并获取新位置的迭代器 (deque 插入可能会使迭代器失效，但返回值指向新元素)
            // 插入中间位置在 deque 中开销是 O(N)，但在 600 个桶的规模下非常快
            auto inserted_it = history_buckets_.insert(it, std::move(new_bucket));
            target_bucket_ptr = &(*inserted_it);

            // 如果插入位置在 window_start_index_ 之前，需要修正 index
            // 比如窗口开始是下标 5，我们在下标 2 插了一个，原来的下标 5 变成了 6
            std::size_t insert_index = std::distance(history_buckets_.begin(), inserted_it);
            if (insert_index <= window_start_index_) {
                window_start_index_++;
            }
        }
    }

    // 获取当前系统的“最新时间” (注意：是队尾的时间，不是当前插入的时间)
    long long current_latest_time = history_buckets_.back().bucket_start_time;
    // 计算窗口有效阈值
    long long expire_threshold = current_latest_time - WINDOW_DURATION_MS;
    
    // 判断这条数据是否还在有效窗口内
    bool is_in_window = (bucket_time >= expire_threshold);

    // 步骤 B: 更新数据
    for (const auto& kv : local_counts) {
        const std::string& w = kv.first;
        int count_inc = kv.second;

        // 1. 更新桶 (历史记录)
        target_bucket_ptr->word_counts[w] += count_inc;

        // 2. 更新全局 (永远更新)
        {
            int old_c = global_counts_[w];
            int new_c = old_c + count_inc;
            global_counts_[w] = new_c;
            UpdateRankingSet(ranking_set_, w, old_c, new_c);
        }

        // 3. 更新窗口 (仅当在窗口期内时更新)
        if (is_in_window) {
            window_counts_[w] += count_inc;
        }
    }

    // 步骤 C: 窗口滑动清理
    while (window_start_index_ < history_buckets_.size()) {
        const auto& old_bucket = history_buckets_[window_start_index_];
        
        // 如果桶的开始时间 早于 (最新时间 - 窗口长度)，则该桶过期
        if (old_bucket.bucket_start_time < expire_threshold) {
            for (const auto& kv : old_bucket.word_counts) {
                const std::string& w = kv.first;
                int remove_c = kv.second;
                
                // 从窗口统计中减去
                if (window_counts_.count(w)) {
                    int old_c = window_counts_[w];
                    int new_c = old_c - remove_c;
                    if (new_c <= 0) {
                        window_counts_.erase(w);
                        new_c = 0;
                    } else {
                        window_counts_[w] = new_c;
                    }
                }
            }
            window_start_index_++;
        } else {
            break;
        }
    }
}

std::vector<std::pair<std::string, int>> Analyzer::GetLast10MinTopK(int k) {
    // 1. 加读锁
    std::shared_lock<std::shared_mutex> lock(mutex_); 
    
    // 2. 边界检查
    if (window_counts_.empty()) return {};

    // 3. 将 Map 拷贝到 Vec
    std::vector<std::pair<int, std::string>> temp_vec;
    temp_vec.reserve(window_counts_.size());
    
    for (const auto& kv : window_counts_) {
        temp_vec.push_back({kv.second, kv.first});
    }

    // 4. 确定 K 的有效范围
    if (k > (int)temp_vec.size()) k = temp_vec.size();

    // 5. 部分排序 (O(N * log K)) - 只排前 K 个
    std::partial_sort(temp_vec.begin(), temp_vec.begin() + k, temp_vec.end(),
        [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
            // 优先按频率降序
            if (a.first != b.first) return a.first > b.first;
            // 频率相同按字典序
            return a.second < b.second;
        });

    // 6. 返回
    std::vector<std::pair<std::string, int>> ans;
    ans.reserve(k);
    for (int i = 0; i < k; ++i) {
        ans.push_back({temp_vec[i].second, temp_vec[i].first});
    }
    return ans;
}

std::vector<std::pair<std::string, int>> Analyzer::GetTopKInTimeRange(long long start_ts, long long end_ts, int k) {
    std::shared_lock<std::shared_mutex> lock(mutex_); 
    
    // 如果历史为空，直接返回
    if (history_buckets_.empty()) return {};

    std::unordered_map<std::string, int> range_counts;

    // 使用二分查找定位起始位置
    auto it_start = std::lower_bound(history_buckets_.begin(), history_buckets_.end(), start_ts,
        [](const TimeBucket& bucket, long long val) {
            return bucket.bucket_start_time < val;
        });

    // 仅遍历目标时间段内的桶 (O(M))
    for (auto it = it_start; it != history_buckets_.end(); ++it) {
        // 如果当前桶的时间已经超过了结束时间，直接停止循环
        if (it->bucket_start_time > end_ts) {
            break;
        }

        // 聚合词频
        for (const auto& kv : it->word_counts) {
            range_counts[kv.first] += kv.second;
        }
    }

    if (range_counts.empty()) return {};

    std::vector<std::pair<int, std::string>> temp_vec;
    temp_vec.reserve(range_counts.size());
    for (const auto& kv : range_counts) {
        temp_vec.push_back({kv.second, kv.first});
    }

    // 防 k 越界
    if (k > (int)temp_vec.size()) k = temp_vec.size();

    // Partial Sort (Top K)
    std::partial_sort(temp_vec.begin(), temp_vec.begin() + k, temp_vec.end(),
        [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
            if (a.first != b.first) return a.first > b.first; // 频次降序
            return a.second < b.second; // 字典序升序
        });

    std::vector<std::pair<std::string, int>> ans;
    ans.reserve(k);
    for (int i = 0; i < k; ++i) {
        ans.push_back({temp_vec[i].second, temp_vec[i].first});
    }
    return ans;
}

/*
    工具函数
*/
void Analyzer::DebugPrint() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::cout << "=== Analyzer State ===" << std::endl;
    std::cout << "Total Buckets: " << history_buckets_.size() << std::endl;
    std::cout << "Global Unique Words: " << global_counts_.size() << std::endl;
    std::cout << "Window (10min) Unique Words: " << window_counts_.size() << std::endl;
    std::cout << "Window Start Index: " << window_start_index_ << std::endl;
    if (!history_buckets_.empty()) {
        std::cout << "Latest Bucket Time: " << history_buckets_.back().bucket_start_time << " ms" << std::endl;
    }
    std::cout << "======================" << std::endl;
}

/*
    当前飙升
*/
std::vector<TrendItem> Analyzer::GetTrending(int k, int min_threshold) {
    std::shared_lock<std::shared_mutex> lock(mutex_); // 读锁

    std::vector<TrendItem> result;
    
    // 1. 确定有效窗口范围
    if (history_buckets_.empty() || window_start_index_ >= history_buckets_.size()) {
        return result;
    }

    // 窗口内的桶数量 N
    long long n = history_buckets_.size() - window_start_index_;
    if (n < 2) return result; // 只有一个点无法计算斜率

    // 2. 预计算 X 的相关和 (X 代表 0 到 n-1 的时间序列)
    // sum_x = 0 + 1 + ... + (n-1) = n*(n-1)/2
    double sum_x = (double)n * (n - 1) / 2.0;
    
    // sum_xx = 0^2 + 1^2 + ... + (n-1)^2 = (n-1)*n*(2n-1)/6
    double sum_xx = (double)(n - 1) * n * (2 * n - 1) / 6.0;

    // 分母: N * Σx² - (Σx)²
    double denominator = n * sum_xx - sum_x * sum_x;
    if (std::abs(denominator) < 1e-9) return result; // 防止除0

    // 3. 计算 Σxy (Sum of X * Y)
    // 遍历桶是必须的：遍历桶，累加 sum_xy 到 map 中。
    std::unordered_map<std::string, double> sum_xy_map;

    // 遍历窗口内的所有桶
    for (size_t i = 0; i < n; ++i) {
        // history_buckets_ 的绝对下标
        size_t bucket_idx = window_start_index_ + i;
        const auto& bucket = history_buckets_[bucket_idx];
        
        // 当前的时间序号 x = i (从0开始)
        double x = (double)i;

        for (const auto& kv : bucket.word_counts) {
            const std::string& word = kv.first;
            int count = kv.second;
            sum_xy_map[word] += x * count;
        }
    }

    // 4. 计算斜率并筛选
    for (const auto& kv : window_counts_) {
        const std::string& word = kv.first;
        int total_count = kv.second; // 这就是 Σy

        // 阈值过滤
        if (total_count < min_threshold) continue;

        // 获取 Σxy，如果没有出现在 map 中默认为 0
        double s_xy = 0.0;
        auto it = sum_xy_map.find(word);
        if (it != sum_xy_map.end()) {
            s_xy = it->second;
        }

        // numerator = N * Σxy - Σx * Σy
        double numerator = n * s_xy - sum_x * total_count;
        double slope = numerator / denominator;

        result.push_back({word, slope, total_count});
    }

    // 5. 排序：按斜率绝对值从大到小 (同时飙升和骤降)
    if (k > (int)result.size()) k = result.size();
    
    std::partial_sort(result.begin(), result.begin() + k, result.end(),
        [](const TrendItem& a, const TrendItem& b) {
            // 按斜率绝对值排序
            if (std::abs(a.slope) != std::abs(b.slope)) {
                return std::abs(a.slope) > std::abs(b.slope);
            }
            // 斜率相同按总词频
            return a.total_count > b.total_count;
        });

    result.resize(k);
    return result;
}