#include "event_sender.h"

#include <cassert>

int main() {
    EventSender sender("http://127.0.0.1:8000/api/v1/events");
    const EventSenderStats initial_stats = sender.stats();
    assert(initial_stats.enqueued == 0);
    assert(initial_stats.sent == 0);
    assert(initial_stats.failed == 0);
    assert(initial_stats.queue_full == 0);

    const std::string run_id = new_event_uuid();
    assert(!run_id.empty());
    const std::string next_run_id = new_event_uuid();
    assert(next_run_id != run_id);

    const EventPayload first = make_line_crossing_event(
        run_id, "red", "car", 497, "BOTTOM_TO_TOP", 0.82f);
    const EventPayload second = make_line_crossing_event(
        run_id, "green", "bus", 498, "LEFT_TO_RIGHT", 0.75f);
    assert(!first.event_id.empty());
    assert(first.event_id != second.event_id);
    assert(first.run_id == run_id);
    assert(second.run_id == run_id);
    assert(first.timestamp.find("Z") != std::string::npos);
    assert(first.camera_id == "source0");
    assert(first.event_type == "line_crossing");
    assert(first.rule_id == "red");
    assert(first.object_class == "car");
    assert(first.track_id == 497);
    assert(first.direction == "BOTTOM_TO_TOP");
    assert(first.has_confidence);

    const EventPayload red_entry = make_red_zone_entry_event(
        run_id, "YELLOW", 611, 0.73f);
    assert(!red_entry.event_id.empty());
    assert(red_entry.run_id == run_id);
    assert(red_entry.camera_id == "source1");
    assert(red_entry.event_type == "red_zone_entry");
    assert(red_entry.rule_id == "case2_red");
    assert(red_entry.object_class == "person");
    assert(red_entry.track_id == 611);
    assert(!red_entry.has_direction);
    assert(red_entry.has_zone_transition);
    assert(red_entry.from_zone == "YELLOW");
    assert(red_entry.to_zone == "RED");
    assert(red_entry.has_confidence);
    const EventPayload retry = first;
    assert(retry.event_id == first.event_id);
    assert(retry.run_id == first.run_id);
    assert(event_delivery_success(200));
    assert(event_delivery_success(201));
    assert(!event_delivery_success(400));
    assert(event_delivery_retryable(0, true));
    assert(event_delivery_retryable(500, false));
    assert(!event_delivery_retryable(400, false));
    assert(event_sender_queue_capacity() == 100);
    return 0;
}