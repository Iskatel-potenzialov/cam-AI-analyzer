#include "live_state_sender.h"

#include <curl/curl.h>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

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

std::size_t discard_response_body(char*, std::size_t size, std::size_t count, void*) {
    return size * count;
}

bool send_snapshot(const std::string& endpoint_url, const LiveStateSnapshot& snapshot, long* http_status, std::string* error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        *http_status = 0;
        *error = "curl_easy_init_failed";
        return false;
    }
    const std::string body = live_state_payload_json(snapshot);
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
    *http_status = 0;
    if (result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, http_status);
    } else {
        *error = curl_easy_strerror(result);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result == CURLE_OK && *http_status == 200;
}

void log_counts(const char* event, const LiveStateSnapshot& snapshot, long http_status, const std::string& error) {
    std::uint64_t red = 0, yellow = 0, green = 0;
    for (std::vector<LiveStatePerson>::const_iterator person = snapshot.people.begin(); person != snapshot.people.end(); ++person) {
        if (person->zone == "RED") ++red;
        else if (person->zone == "YELLOW") ++yellow;
        else if (person->zone == "GREEN") ++green;
    }
    std::ostream& stream = std::string(event) == "LIVE_STATE_SENT" ? std::cout : std::cerr;
    stream << event << " camera_id=" << snapshot.camera_id << " red=" << red << " yellow=" << yellow
           << " green=" << green << " people=" << snapshot.people.size();
    if (http_status != 0) stream << " http_status=" << http_status;
    if (!error.empty()) stream << " error=" << error;
    stream << "\n";
}

}  // namespace

std::string live_state_payload_json(const LiveStateSnapshot& snapshot) {
    std::uint64_t red = 0, yellow = 0, green = 0;
    std::ostringstream people;
    people << "[";
    for (std::vector<LiveStatePerson>::const_iterator person = snapshot.people.begin(); person != snapshot.people.end(); ++person) {
        if (person != snapshot.people.begin()) people << ",";
        if (person->zone == "RED") ++red;
        else if (person->zone == "YELLOW") ++yellow;
        else if (person->zone == "GREEN") ++green;
        people << "{\"track_id\":" << person->track_id << ",\"zone\":\"" << json_escape(person->zone) << "\",\"confidence\":";
        if (person->has_confidence && std::isfinite(person->confidence) && person->confidence >= 0.0f && person->confidence <= 1.0f) {
            people << std::fixed << std::setprecision(6) << person->confidence;
        } else {
            people << "null";
        }
        people << "}";
    }
    people << "]";
    std::ostringstream json;
    json << "{\"camera_id\":\"" << json_escape(snapshot.camera_id) << "\",\"counts\":{\"red\":" << red
         << ",\"yellow\":" << yellow << ",\"green\":" << green << "},\"people\":" << people.str() << "}";
    return json.str();
}

void replace_live_state_snapshot(LiveStateSnapshot* pending_snapshot, const LiveStateSnapshot& snapshot) {
    *pending_snapshot = snapshot;
}

LiveStateSender::LiveStateSender(const std::string& endpoint_url)
    : endpoint_url_(endpoint_url), has_pending_(false), accepting_(false), stopping_(false), started_(false), published_(0), sent_(0), failed_(0) {
}

LiveStateSender::~LiveStateSender() {
    stop();
}

bool LiveStateSender::start() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        std::cerr << "LIVE_STATE_SEND_FAILED reason=curl_global_init_failed\n";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = true;
        stopping_ = false;
        started_ = true;
    }
    worker_ = std::thread(&LiveStateSender::worker_loop, this);
    return true;
}

bool LiveStateSender::publish(const LiveStateSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!accepting_) return false;
    replace_live_state_snapshot(&pending_snapshot_, snapshot);
    has_pending_ = true;
    published_.fetch_add(1);
    condition_.notify_one();
    return true;
}

LiveStateSenderStats LiveStateSender::stats() const {
    LiveStateSenderStats result;
    result.published = published_.load();
    result.sent = sent_.load();
    result.failed = failed_.load();
    return result;
}

void LiveStateSender::worker_loop() {
    for (;;) {
        LiveStateSnapshot snapshot;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || has_pending_; });
            if (!has_pending_) return;
            snapshot = pending_snapshot_;
            has_pending_ = false;
        }
        long http_status = 0;
        std::string error;
        if (send_snapshot(endpoint_url_, snapshot, &http_status, &error)) {
            sent_.fetch_add(1);
            log_counts("LIVE_STATE_SENT", snapshot, http_status, "");
        } else {
            failed_.fetch_add(1);
            log_counts("LIVE_STATE_SEND_FAILED", snapshot, http_status, error.empty() ? "http_status" : error);
        }
    }
}

void LiveStateSender::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) return;
        accepting_ = false;
        stopping_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
    curl_global_cleanup();
    std::lock_guard<std::mutex> lock(mutex_);
    started_ = false;
}