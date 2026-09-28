#include "live_state_sender.h"

#include <cassert>
#include <limits>

int main() {
    LiveStateSnapshot snapshot;
    snapshot.camera_id = "source1";
    snapshot.people.push_back(LiveStatePerson{107, "RED", 0.73f, true});
    snapshot.people.push_back(LiveStatePerson{108, "YELLOW", 0.0f, false});
    snapshot.people.push_back(LiveStatePerson{109, "GREEN", 0.64f, true});
    const std::string json = live_state_payload_json(snapshot);
    assert(json.find("\"camera_id\":\"source1\"") != std::string::npos);
    assert(json.find("\"red\":1,\"yellow\":1,\"green\":1") != std::string::npos);
    assert(json.find("\"track_id\":107,\"zone\":\"RED\",\"confidence\":0.730000") != std::string::npos);
    assert(json.find("\"track_id\":108,\"zone\":\"YELLOW\",\"confidence\":null") != std::string::npos);
    assert(json.find("\"track_id\":109,\"zone\":\"GREEN\",\"confidence\":0.640000") != std::string::npos);

    LiveStateSnapshot empty;
    empty.camera_id = "source1";
    const std::string empty_json = live_state_payload_json(empty);
    assert(empty_json.find("\"red\":0,\"yellow\":0,\"green\":0") != std::string::npos);
    assert(empty_json.find("\"people\":[]") != std::string::npos);

    LiveStateSnapshot latest = empty;
    latest.people.push_back(LiveStatePerson{110, "GREEN", std::numeric_limits<float>::quiet_NaN(), true});
    const std::string latest_json = live_state_payload_json(latest);
    assert(latest_json.find("\"green\":1") != std::string::npos);
    assert(latest_json.find("\"confidence\":null") != std::string::npos);

    LiveStateSnapshot pending = snapshot;
    replace_live_state_snapshot(&pending, latest);
    assert(pending.camera_id == "source1");
    assert(pending.people.size() == 1);
    assert(pending.people[0].track_id == 110);
    assert(pending.people[0].zone == "GREEN");
    return 0;
}