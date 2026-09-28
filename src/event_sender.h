#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <random>
#include <string>
#include <cstddef>
#include <thread>

struct EventPayload {
    std::string event_id;
    std::string run_id;
    std::string timestamp;
    std::string camera_id;
    std::string event_type;
    std::string rule_id;
    std::string object_class;
    std::uint64_t track_id;
    std::string direction;
    std::string from_zone;
    std::string to_zone;
    float confidence;
    bool has_confidence;
    bool has_direction;
    bool has_zone_transition;
};

struct EventSenderStats {
    std::uint64_t enqueued = 0;
    std::uint64_t sent = 0;
    std::uint64_t failed = 0;
    std::uint64_t queue_full = 0;
};

std::string new_event_uuid();

bool event_delivery_success(long http_status);
bool event_delivery_retryable(long http_status, bool transport_error);
std::size_t event_sender_queue_capacity();

EventPayload make_line_crossing_event(
    const std::string& run_id,
    const std::string& rule_id,
    const std::string& object_class,
    std::uint64_t track_id,
    const std::string& direction,
    float confidence);

EventPayload make_red_zone_entry_event(
    const std::string& run_id,
    const std::string& from_zone,
    std::uint64_t track_id,
    float confidence);

class EventSender {
public:
    explicit EventSender(const std::string& endpoint_url);
    ~EventSender();

    bool start();
    bool enqueue(const EventPayload& event);
    EventSenderStats stats() const;
    void stop();

private:
    void worker_loop();
    bool stopping() const;

    const std::string endpoint_url_;
    std::deque<EventPayload> queue_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    bool accepting_;
    bool stopping_;
    bool started_;
    std::atomic<std::uint64_t> enqueued_;
    std::atomic<std::uint64_t> sent_;
    std::atomic<std::uint64_t> failed_;
    std::atomic<std::uint64_t> queue_full_;
    std::chrono::steady_clock::time_point stop_deadline_;
};