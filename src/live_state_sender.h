#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct LiveStatePerson {
    std::uint64_t track_id;
    std::string zone;
    float confidence;
    bool has_confidence;
};

struct LiveStateSnapshot {
    std::string camera_id;
    std::vector<LiveStatePerson> people;
};

struct LiveStateSenderStats {
    std::uint64_t published = 0;
    std::uint64_t sent = 0;
    std::uint64_t failed = 0;
};

class LiveStateSender {
public:
    explicit LiveStateSender(const std::string& endpoint_url);
    ~LiveStateSender();

    bool start();
    bool publish(const LiveStateSnapshot& snapshot);
    LiveStateSenderStats stats() const;
    void stop();

private:
    void worker_loop();

    const std::string endpoint_url_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    LiveStateSnapshot pending_snapshot_;
    bool has_pending_;
    bool accepting_;
    bool stopping_;
    bool started_;
    std::atomic<std::uint64_t> published_;
    std::atomic<std::uint64_t> sent_;
    std::atomic<std::uint64_t> failed_;
};

std::string live_state_payload_json(const LiveStateSnapshot& snapshot);
void replace_live_state_snapshot(LiveStateSnapshot* pending_snapshot, const LiveStateSnapshot& snapshot);