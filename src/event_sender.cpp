#include "event_sender.h"

#include <curl/curl.h>

#include <cmath>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

const std::size_t kMaxQueueSize = 100;
const unsigned int kMaxAttempts = 3;

std::string utc_timestamp() {
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::chrono::system_clock::duration since_epoch = now.time_since_epoch();
    const long milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count() % 1000;
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc_time;
    gmtime_r(&seconds, &utc_time);

    char date_time[32];
    std::strftime(date_time, sizeof(date_time), "%Y-%m-%dT%H:%M:%S", &utc_time);
    std::ostringstream value;
    value << date_time << "." << std::setfill('0') << std::setw(3) << milliseconds << "Z";
    return value.str();
}

std::string new_uuid() {
    static std::mutex uuid_mutex;
    static std::mt19937_64 generator(
        static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::lock_guard<std::mutex> lock(uuid_mutex);
    const std::uint64_t high = generator();
    const std::uint64_t low = generator();

    unsigned char bytes[16];
    for (unsigned int index = 0; index < 8; ++index) {
        bytes[index] = static_cast<unsigned char>((high >> (56 - 8 * index)) & 0xff);
        bytes[index + 8] = static_cast<unsigned char>((low >> (56 - 8 * index)) & 0xff);
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);

    char uuid[37];
    std::snprintf(
        uuid,
        sizeof(uuid),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return uuid;
}

std::string json_escape(const std::string& value) {
    std::ostringstream escaped;
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        switch (*it) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default: escaped << *it; break;
        }
    }
    return escaped.str();
}

std::string payload_json(const EventPayload& event) {
    std::ostringstream json;
    json << "{\"event_id\":\"" << json_escape(event.event_id)
         << "\",\"schema_version\":1"
         << ",\"timestamp\":\"" << json_escape(event.timestamp)
         << "\",\"camera_id\":\"" << json_escape(event.camera_id)
         << "\",\"event_type\":\"" << json_escape(event.event_type)
         << "\",\"rule_id\":\"" << json_escape(event.rule_id)
         << "\",\"object_class\":\"" << json_escape(event.object_class)
         << "\",\"track_id\":" << event.track_id;
    if (event.has_direction) {
        json << ",\"direction\":\"" << json_escape(event.direction) << "\"";
    }
    if (event.has_confidence) {
        json << ",\"confidence\":" << std::fixed << std::setprecision(6) << event.confidence;
    }
    json << ",\"attributes\":{\"run_id\":\"" << json_escape(event.run_id) << "\"";
    if (event.has_zone_transition) {
        json << ",\"from_zone\":\"" << json_escape(event.from_zone)
             << "\",\"to_zone\":\"" << json_escape(event.to_zone) << "\"";
    }
    json << "}}";
    return json.str();
}
std::string event_log_fields(const EventPayload& event) {
    std::ostringstream fields;
    fields << "event_id=" << event.event_id
           << " camera_id=" << event.camera_id
           << " rule_id=" << event.rule_id
           << " track_id=" << event.track_id
           << " direction=" << event.direction;
    return fields.str();
}

std::size_t discard_response_body(char*, std::size_t size, std::size_t count, void*) {
    return size * count;
}
struct DeliveryResult {
    bool success;
    bool retryable;
    long http_status;
    std::string error;
};

DeliveryResult send_once(const std::string& endpoint_url, const EventPayload& event) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return DeliveryResult{false, true, 0, "curl_easy_init_failed"};
    }

    const std::string body = payload_json(event);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, endpoint_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 1500L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response_body);

    const CURLcode result = curl_easy_perform(curl);
    long http_status = 0;
    if (result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        return DeliveryResult{false, event_delivery_retryable(0, true), 0, curl_easy_strerror(result)};
    }
    if (event_delivery_success(http_status)) {
        return DeliveryResult{true, false, http_status, ""};
    }
    return DeliveryResult{false, event_delivery_retryable(http_status, false), http_status, "http_status"};
}

}  // namespace

std::string new_event_uuid() {
    return new_uuid();
}

bool event_delivery_success(long http_status) {
    return http_status == 200 || http_status == 201;
}

bool event_delivery_retryable(long http_status, bool transport_error) {
    return transport_error || http_status >= 500;
}

std::size_t event_sender_queue_capacity() {
    return kMaxQueueSize;
}
EventPayload make_line_crossing_event(
    const std::string& run_id,
    const std::string& rule_id,
    const std::string& object_class,
    std::uint64_t track_id,
    const std::string& direction,
    float confidence) {
    EventPayload event;
    event.event_id = new_event_uuid();
    event.run_id = run_id;
    event.timestamp = utc_timestamp();
    event.camera_id = "source0";
    event.event_type = "line_crossing";
    event.rule_id = rule_id;
    event.object_class = object_class;
    event.track_id = track_id;
    event.direction = direction;
    event.confidence = confidence;
    event.has_confidence = std::isfinite(confidence) && confidence >= 0.0f && confidence <= 1.0f;
    event.has_direction = true;
    event.has_zone_transition = false;
    return event;
}

EventPayload make_red_zone_entry_event(
    const std::string& run_id,
    const std::string& from_zone,
    std::uint64_t track_id,
    float confidence) {
    EventPayload event;
    event.event_id = new_event_uuid();
    event.run_id = run_id;
    event.timestamp = utc_timestamp();
    event.camera_id = "source1";
    event.event_type = "red_zone_entry";
    event.rule_id = "case2_red";
    event.object_class = "person";
    event.track_id = track_id;
    event.confidence = confidence;
    event.has_confidence = std::isfinite(confidence) && confidence >= 0.0f && confidence <= 1.0f;
    event.has_direction = false;
    event.from_zone = from_zone;
    event.to_zone = "RED";
    event.has_zone_transition = true;
    return event;
}
EventSender::EventSender(const std::string& endpoint_url)
    : endpoint_url_(endpoint_url), accepting_(false), stopping_(false), started_(false), enqueued_(0), sent_(0), failed_(0), queue_full_(0) {
}

EventSender::~EventSender() {
    stop();
}

bool EventSender::start() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        std::cerr << "EVENT_SEND_FAILED reason=curl_global_init_failed\n";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = true;
        stopping_ = false;
        started_ = true;
    }
    worker_ = std::thread(&EventSender::worker_loop, this);
    return true;
}

bool EventSender::enqueue(const EventPayload& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepting_) {
        return false;
    }
    if (queue_.size() >= kMaxQueueSize) {
        queue_full_.fetch_add(1);
        std::cerr << "EVENT_QUEUE_FULL " << event_log_fields(event) << "\n";
        return false;
    }
    queue_.push_back(event);
    enqueued_.fetch_add(1);
    std::cout << "EVENT_ENQUEUE " << event_log_fields(event) << "\n";
    condition_.notify_one();
    return true;
}

EventSenderStats EventSender::stats() const {
    EventSenderStats result;
    result.enqueued = enqueued_.load();
    result.sent = sent_.load();
    result.failed = failed_.load();
    result.queue_full = queue_full_.load();
    return result;
}
bool EventSender::stopping() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopping_;
}

void EventSender::worker_loop() {
    for (;;) {
        EventPayload event;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && std::chrono::steady_clock::now() >= stop_deadline_) {
                const std::size_t dropped = queue_.size();
                queue_.clear();
                if (dropped != 0) {
                    failed_.fetch_add(dropped);
                    std::cerr << "EVENT_SEND_FAILED reason=shutdown_drain_timeout dropped_events=" << dropped << "\n";
                }
                return;
            }
            if (queue_.empty()) {
                return;
            }
            event = queue_.front();
            queue_.pop_front();
        }

        DeliveryResult delivery{false, false, 0, ""};
        for (unsigned int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            delivery = send_once(endpoint_url_, event);
            if (delivery.success) {
                sent_.fetch_add(1);
                std::cout << "EVENT_SENT " << event_log_fields(event)
                          << " http_status=" << delivery.http_status << "\n";
                break;
            }
            if (!delivery.retryable || attempt == kMaxAttempts || stopping()) {
                failed_.fetch_add(1);
                std::cerr << "EVENT_SEND_FAILED " << event_log_fields(event);
                if (delivery.http_status != 0) {
                    std::cerr << " http_status=" << delivery.http_status;
                } else {
                    std::cerr << " transport_error=" << delivery.error;
                }
                std::cerr << "\n";
                break;
            }
            std::cerr << "EVENT_RETRY " << event_log_fields(event)
                      << " attempt=" << (attempt + 1);
            if (delivery.http_status != 0) {
                std::cerr << " http_status=" << delivery.http_status;
            } else {
                std::cerr << " transport_error=" << delivery.error;
            }
            std::cerr << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
        }
    }
}

void EventSender::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) {
            return;
        }
        accepting_ = false;
        stopping_ = true;
        stop_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    curl_global_cleanup();
    std::lock_guard<std::mutex> lock(mutex_);
    started_ = false;
}