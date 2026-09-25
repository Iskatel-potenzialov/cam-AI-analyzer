#include <gst/gst.h>

#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "gstnvdsmeta.h"
#include "nvdsmeta.h"

namespace {

constexpr guint kDetectionFrameLogLimit = 20;
volatile std::sig_atomic_t g_interrupted = 0;

struct ProbeState {
    std::vector<std::string> labels;
    guint logged_detection_frames = 0;
};

void handle_sigint(int) {
    g_interrupted = 1;
}

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

void set_display_text(NvDsObjectMeta *object_meta,
                      const std::vector<std::string> &labels) {
    std::ostringstream text;
    text << label_for_object(object_meta, labels) << ' ' << std::fixed
         << std::setprecision(2) << object_meta->confidence << " ID:"
         << object_meta->object_id;

    g_free(object_meta->text_params.display_text);
    object_meta->text_params.display_text = g_strdup(text.str().c_str());
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
         frame_list != nullptr; frame_list = frame_list->next) {
        auto *frame_meta = static_cast<NvDsFrameMeta *>(frame_list->data);
        if (frame_meta == nullptr) {
            continue;
        }

        const bool log_frame = frame_meta->num_obj_meta > 0 &&
                               state->logged_detection_frames < kDetectionFrameLogLimit;
        if (log_frame) {
            std::cout << "frame=" << frame_meta->frame_num
                      << " source_id=" << frame_meta->source_id
                      << " objects=" << frame_meta->num_obj_meta << '\n';
        }

        for (NvDsMetaList *object_list = frame_meta->obj_meta_list;
             object_list != nullptr; object_list = object_list->next) {
            auto *object_meta = static_cast<NvDsObjectMeta *>(object_list->data);
            if (object_meta == nullptr) {
                continue;
            }

            set_display_text(object_meta, state->labels);
            if (log_frame) {
                std::cout << "  object_id=" << object_meta->object_id
                          << " class_id=" << object_meta->class_id
                          << " label=" << label_for_object(object_meta, state->labels)
                          << " confidence=" << object_meta->confidence << '\n';
            }
        }

        if (log_frame) {
            ++state->logged_detection_frames;
        }
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

void on_rtsp_pad_added(GstElement *, GstPad *new_pad, gpointer user_data) {
    auto *depay = GST_ELEMENT(user_data);
    GstCaps *caps = gst_pad_get_current_caps(new_pad);
    if (caps == nullptr) {
        caps = gst_pad_query_caps(new_pad, nullptr);
    }

    const GstStructure *structure = caps == nullptr ? nullptr : gst_caps_get_structure(caps, 0);
    const gchar *name = structure == nullptr ? nullptr : gst_structure_get_name(structure);
    const gchar *media = structure == nullptr ? nullptr : gst_structure_get_string(structure, "media");
    const gchar *encoding_name =
        structure == nullptr ? nullptr : gst_structure_get_string(structure, "encoding-name");
    const bool is_h264_video_rtp = g_strcmp0(name, "application/x-rtp") == 0 &&
                                   g_strcmp0(media, "video") == 0 &&
                                   g_strcmp0(encoding_name, "H264") == 0;

    if (!is_h264_video_rtp) {
        std::cout << "Ignoring non-H.264 video RTP RTSP pad\n";
        if (caps != nullptr) {
            gst_caps_unref(caps);
        }
        return;
    }

    GstPad *depay_sink_pad = gst_element_get_static_pad(depay, "sink");
    if (depay_sink_pad == nullptr) {
        std::cerr << "ERROR: could not get rtph264depay sink pad\n";
    } else if (gst_pad_is_linked(depay_sink_pad)) {
        std::cerr << "ERROR: received an additional H.264 video RTP pad\n";
    } else if (gst_pad_link(new_pad, depay_sink_pad) != GST_PAD_LINK_OK) {
        std::cerr << "ERROR: could not link H.264 RTP pad to rtph264depay\n";
    } else {
        std::cout << "Linked H.264 video RTP pad to rtph264depay\n";
    }

    if (depay_sink_pad != nullptr) {
        gst_object_unref(depay_sink_pad);
    }
    if (caps != nullptr) {
        gst_caps_unref(caps);
    }
}

}  // namespace

int main(int argc, char *argv[]) {
    if (argc != 6) {
        std::cerr << "Usage: " << argv[0]
                  << " <rtsp-uri> <nvinfer-config.txt> <coco-labels.txt>"
                  << " <tracker-library.so> <tracker-config.yml>\n";
        return 2;
    }

    gst_init(&argc, &argv);

    ProbeState state;
    state.labels = load_labels(argv[3]);
    if (state.labels.empty()) {
        std::cerr << "ERROR: could not read labels: " << argv[3] << '\n';
        return 1;
    }

    GstElement *pipeline = gst_pipeline_new("stage4b-rtsp-display-pipeline");
    GstElement *source = gst_element_factory_make("rtspsrc", "source");
    GstElement *depay = gst_element_factory_make("rtph264depay", "depay");
    GstElement *parser = gst_element_factory_make("h264parse", "parser");
    GstElement *decoder = gst_element_factory_make("nvv4l2decoder", "decoder");
    GstElement *streammux = gst_element_factory_make("nvstreammux", "streammux");
    GstElement *infer = gst_element_factory_make("nvinfer", "infer");
    GstElement *tracker = gst_element_factory_make("nvtracker", "tracker");
    GstElement *convert = gst_element_factory_make("nvvideoconvert", "convert");
    GstElement *capsfilter = gst_element_factory_make("capsfilter", "rgba-nvmm-caps");
    GstElement *osd = gst_element_factory_make("nvdsosd", "osd");
    GstElement *sink = gst_element_factory_make("nveglglessink", "display");

    if (pipeline == nullptr || source == nullptr || depay == nullptr || parser == nullptr ||
        decoder == nullptr || streammux == nullptr || infer == nullptr || tracker == nullptr ||
        convert == nullptr || capsfilter == nullptr || osd == nullptr || sink == nullptr) {
        std::cerr << "ERROR: could not create a required GStreamer element\n";
        return 1;
    }

    GstCaps *rgba_nvmm_caps =
        gst_caps_from_string("video/x-raw(memory:NVMM),format=RGBA");
    if (rgba_nvmm_caps == nullptr) {
        std::cerr << "ERROR: could not create RGBA/NVMM caps\n";
        gst_object_unref(pipeline);
        return 1;
    }

    g_object_set(source, "location", argv[1], "latency", 200, nullptr);
    gst_util_set_object_arg(G_OBJECT(source), "protocols", "tcp");
    g_signal_connect(source, "pad-added", G_CALLBACK(on_rtsp_pad_added), depay);
    g_object_set(streammux, "live-source", TRUE, "batch-size", 1, "width", 1280,
                 "height", 720, "batched-push-timeout", 4000000, nullptr);
    g_object_set(infer, "config-file-path", argv[2], nullptr);
    g_object_set(tracker, "tracker-width", 960, "tracker-height", 544, "gpu-id", 0,
                 "ll-lib-file", argv[4], "ll-config-file", argv[5], nullptr);
    g_object_set(capsfilter, "caps", rgba_nvmm_caps, nullptr);
    gst_caps_unref(rgba_nvmm_caps);

    gst_bin_add_many(GST_BIN(pipeline), source, depay, parser, decoder, streammux, infer,
                     tracker, convert, capsfilter, osd, sink, nullptr);
    if (!gst_element_link_many(depay, parser, decoder, nullptr) ||
        !link_to_streammux(decoder, streammux) ||
        !gst_element_link_many(streammux, infer, tracker, convert, capsfilter, osd, sink,
                               nullptr)) {
        std::cerr << "ERROR: could not link the pipeline\n";
        gst_object_unref(pipeline);
        return 1;
    }

    GstPad *tracker_src_pad = gst_element_get_static_pad(tracker, "src");
    if (tracker_src_pad == nullptr) {
        std::cerr << "ERROR: could not get nvtracker source pad\n";
        gst_object_unref(pipeline);
        return 1;
    }
    gst_pad_add_probe(tracker_src_pad, GST_PAD_PROBE_TYPE_BUFFER, metadata_probe, &state,
                      nullptr);
    gst_object_unref(tracker_src_pad);

    std::signal(SIGINT, handle_sigint);
    std::cout << "Pipeline: rtspsrc -> rtph264depay -> h264parse -> nvv4l2decoder"
              << " -> nvstreammux -> nvinfer -> nvtracker -> nvvideoconvert"
              << " -> capsfilter(RGBA/NVMM) -> nvdsosd -> nveglglessink\n";
    std::cout << "Probe: nvtracker:src; logging at most " << kDetectionFrameLogLimit
              << " frames with detections\n";
    std::cout << "Press Ctrl+C to stop the live pipeline cleanly.\n";

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "ERROR: could not set pipeline to PLAYING\n";
        gst_object_unref(pipeline);
        return 1;
    }

    GstBus *bus = gst_element_get_bus(pipeline);
    int exit_code = 0;
    bool done = false;

    while (!done && g_interrupted == 0) {
        GstMessage *message = gst_bus_timed_pop_filtered(
            bus, 100 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (message == nullptr) {
            continue;
        }

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

    if (g_interrupted != 0) {
        std::cout << "SIGINT received; stopping pipeline.\n";
    }

    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_element_get_state(pipeline, nullptr, nullptr, GST_CLOCK_TIME_NONE);
    gst_object_unref(pipeline);
    return exit_code;
}