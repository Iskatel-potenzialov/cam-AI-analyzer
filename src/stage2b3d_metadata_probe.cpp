#include <gst/gst.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "gstnvdsmeta.h"
#include "nvdsmeta.h"

namespace {

constexpr guint kDetectionFrameLogLimit = 20;

struct ProbeState {
    std::vector<std::string> labels;
    guint logged_detection_frames = 0;
    bool tracker_enabled = false;
};

std::vector<std::string> load_labels(const char *path) {
    std::ifstream input(path);
    std::vector<std::string> labels;
    std::string label;

    if (!input) {
        return labels;
    }

    while (std::getline(input, label)) {
        labels.push_back(label);
    }

    return labels;
}

const char *label_for_object(const NvDsObjectMeta *object_meta,
                             const std::vector<std::string> &labels) {
    if (object_meta->obj_label[0] != '\0') {
        return object_meta->obj_label;
    }

    if (object_meta->class_id >= 0 &&
        static_cast<size_t>(object_meta->class_id) < labels.size()) {
        return labels[object_meta->class_id].c_str();
    }

    return "unknown";
}

GstPadProbeReturn metadata_probe(GstPad *, GstPadProbeInfo *info,
                                 gpointer user_data) {
    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
        return GST_PAD_PROBE_OK;
    }

    auto *state = static_cast<ProbeState *>(user_data);
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
    if (batch_meta == nullptr) {
        return GST_PAD_PROBE_OK;
    }

    for (NvDsMetaList *frame_list = batch_meta->frame_meta_list;
         frame_list != nullptr && state->logged_detection_frames < kDetectionFrameLogLimit;
         frame_list = frame_list->next) {
        auto *frame_meta = static_cast<NvDsFrameMeta *>(frame_list->data);
        if (frame_meta == nullptr || frame_meta->num_obj_meta == 0) {
            continue;
        }

        std::cout << "frame=" << frame_meta->frame_num
                  << " objects=" << frame_meta->num_obj_meta << '\n';

        for (NvDsMetaList *object_list = frame_meta->obj_meta_list;
             object_list != nullptr; object_list = object_list->next) {
            auto *object_meta = static_cast<NvDsObjectMeta *>(object_list->data);
            if (object_meta == nullptr) {
                continue;
            }

            std::cout << "  ";
            if (state->tracker_enabled) {
                std::cout << "object_id=" << object_meta->object_id << ' ';
            }
            std::cout << "class_id=" << object_meta->class_id
                      << " label=" << label_for_object(object_meta, state->labels)
                      << " confidence=" << object_meta->confidence << '\n';
        }

        ++state->logged_detection_frames;
    }

    return GST_PAD_PROBE_OK;
}

bool link_to_streammux(GstElement *decoder, GstElement *streammux) {
    GstPad *decoder_src_pad = gst_element_get_static_pad(decoder, "src");
    GstPad *streammux_sink_pad = gst_element_request_pad_simple(streammux, "sink_0");
    if (decoder_src_pad == nullptr || streammux_sink_pad == nullptr) {
        if (decoder_src_pad != nullptr) {
            gst_object_unref(decoder_src_pad);
        }
        if (streammux_sink_pad != nullptr) {
            gst_object_unref(streammux_sink_pad);
        }
        return false;
    }

    const GstPadLinkReturn link_result = gst_pad_link(decoder_src_pad, streammux_sink_pad);
    gst_object_unref(decoder_src_pad);
    gst_object_unref(streammux_sink_pad);
    return link_result == GST_PAD_LINK_OK;
}

}  // namespace

int main(int argc, char *argv[]) {
    if (argc != 4 && argc != 6) {
        std::cerr << "Usage: " << argv[0]
                  << " <video.h264> <nvinfer-config.txt> <coco-labels.txt>"
                  << " [<tracker-library.so> <tracker-config.yml>]\n";
        return 2;
    }

    const bool tracker_enabled = argc == 6;
    gst_init(&argc, &argv);

    ProbeState state;
    state.labels = load_labels(argv[3]);
    state.tracker_enabled = tracker_enabled;
    if (state.labels.empty()) {
        std::cerr << "ERROR: could not read labels: " << argv[3] << '\n';
        return 1;
    }

    GstElement *pipeline = gst_pipeline_new("stage2b3d-metadata-pipeline");
    GstElement *source = gst_element_factory_make("filesrc", "source");
    GstElement *parser = gst_element_factory_make("h264parse", "parser");
    GstElement *decoder = gst_element_factory_make("nvv4l2decoder", "decoder");
    GstElement *streammux = gst_element_factory_make("nvstreammux", "streammux");
    GstElement *infer = gst_element_factory_make("nvinfer", "infer");
    GstElement *tracker = tracker_enabled
                              ? gst_element_factory_make("nvtracker", "tracker")
                              : nullptr;
    GstElement *sink = gst_element_factory_make("fakesink", "sink");

    if (pipeline == nullptr || source == nullptr || parser == nullptr ||
        decoder == nullptr || streammux == nullptr || infer == nullptr || sink == nullptr ||
        (tracker_enabled && tracker == nullptr)) {
        std::cerr << "ERROR: could not create a required GStreamer element\n";
        return 1;
    }

    g_object_set(source, "location", argv[1], nullptr);
    g_object_set(streammux, "batch-size", 1, "width", 1280, "height", 720,
                 "batched-push-timeout", 4000000, nullptr);
    g_object_set(infer, "config-file-path", argv[2], nullptr);
    if (tracker_enabled) {
        g_object_set(tracker, "tracker-width", 960, "tracker-height", 544, "gpu-id", 0,
                     "ll-lib-file", argv[4], "ll-config-file", argv[5], nullptr);
    }
    g_object_set(sink, "sync", FALSE, nullptr);

    gst_bin_add_many(GST_BIN(pipeline), source, parser, decoder, streammux, infer, nullptr);
    if (tracker_enabled) {
        gst_bin_add_many(GST_BIN(pipeline), tracker, sink, nullptr);
    } else {
        gst_bin_add(GST_BIN(pipeline), sink);
    }

    const gboolean post_infer_linked = tracker_enabled
                                           ? gst_element_link_many(infer, tracker, sink, nullptr)
                                           : gst_element_link_many(infer, sink, nullptr);
    if (!gst_element_link_many(source, parser, decoder, nullptr) ||
        !link_to_streammux(decoder, streammux) || !post_infer_linked ||
        !gst_element_link(streammux, infer)) {
        std::cerr << "ERROR: could not link the pipeline\n";
        gst_object_unref(pipeline);
        return 1;
    }

    GstElement *probe_element = tracker_enabled ? tracker : infer;
    GstPad *probe_src_pad = gst_element_get_static_pad(probe_element, "src");
    if (probe_src_pad == nullptr) {
        std::cerr << "ERROR: could not get metadata probe source pad\n";
        gst_object_unref(pipeline);
        return 1;
    }
    gst_pad_add_probe(probe_src_pad, GST_PAD_PROBE_TYPE_BUFFER, metadata_probe, &state,
                      nullptr);
    gst_object_unref(probe_src_pad);

    std::cout << "Pipeline: filesrc -> h264parse -> nvv4l2decoder -> nvstreammux -> nvinfer";
    if (tracker_enabled) {
        std::cout << " -> nvtracker";
    }
    std::cout << " -> fakesink\n";
    std::cout << "Probe: " << (tracker_enabled ? "nvtracker:src" : "nvinfer:src")
              << "; logging at most " << kDetectionFrameLogLimit
              << " frames with detections\n";

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "ERROR: could not set pipeline to PLAYING\n";
        gst_object_unref(pipeline);
        return 1;
    }

    GstBus *bus = gst_element_get_bus(pipeline);
    int exit_code = 0;
    bool done = false;

    while (!done) {
        GstMessage *message = gst_bus_timed_pop_filtered(
            bus, GST_CLOCK_TIME_NONE,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
            std::cout << "EOS\n";
        } else {
            GError *error = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            std::cerr << "ERROR: " << error->message << '\n';
            g_clear_error(&error);
            g_free(debug);
            exit_code = 1;
        }

        gst_message_unref(message);
        done = true;
    }

    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return exit_code;
}