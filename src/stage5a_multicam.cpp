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

constexpr guint kSourceCount = 2;
constexpr guint kDetectionFrameLogLimit = 20;
volatile std::sig_atomic_t g_interrupted = 0;

struct AppState {
    std::vector<std::string> labels;
    guint logged_detection_frames[kSourceCount] = {0, 0};
    GstPad *mux_sink_pads[kSourceCount] = {nullptr, nullptr};
};

struct DecoderProbeState {
    guint source_id;
    guint64 decoded_buffers = 0;
    bool caps_logged = false;
};

struct HlsLinkState {
    GstElement *tsdemux;
    GstElement *parser;
    bool mpegts_linked = false;
    bool h264_linked = false;
};

void handle_sigint(int) {
    g_interrupted = 1;
}

std::vector<std::string> load_labels(const char *path) {
    std::ifstream input(path);
    std::vector<std::string> labels;
    std::string label;
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

std::string redact_secret(const gchar *text, const char *first_secret,
                          const char *second_secret) {
    std::string result = text == nullptr ? "" : text;
    const char *secrets[] = {first_secret, second_secret};
    for (const char *secret_text : secrets) {
        const std::string secret = secret_text == nullptr ? "" : secret_text;
        if (secret.empty()) {
            continue;
        }
        std::string::size_type position = 0;
        while ((position = result.find(secret, position)) != std::string::npos) {
            result.replace(position, secret.size(), "<redacted-source-url>");
            position += sizeof("<redacted-source-url>") - 1;
        }
    }
    return result;
}

void post_stream_error(GstElement *element, const char *message) {
    GError *error = g_error_new_literal(GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, message);
    GstMessage *bus_message = gst_message_new_error(GST_OBJECT(element), error, nullptr);
    g_error_free(error);
    gst_element_post_message(element, bus_message);
}

GstCaps *current_or_query_caps(GstPad *pad) {
    GstCaps *caps = gst_pad_get_current_caps(pad);
    return caps == nullptr ? gst_pad_query_caps(pad, nullptr) : caps;
}

void log_caps(const char *prefix, GstCaps *caps) {
    gchar *caps_text = caps == nullptr ? nullptr : gst_caps_to_string(caps);
    std::cout << prefix << (caps_text == nullptr ? "<unknown>" : caps_text) << '\n';
    g_free(caps_text);
}

bool caps_intersect(GstCaps *caps, const char *expected_caps) {
    GstCaps *expected = gst_caps_from_string(expected_caps);
    const bool result = caps != nullptr && expected != nullptr &&
                        gst_caps_can_intersect(caps, expected);
    if (expected != nullptr) {
        gst_caps_unref(expected);
    }
    return result;
}

GstPadProbeReturn decoder_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
        return GST_PAD_PROBE_OK;
    }
    auto *state = static_cast<DecoderProbeState *>(user_data);
    ++state->decoded_buffers;
    if (!state->caps_logged) {
        GstCaps *caps = current_or_query_caps(pad);
        std::cout << "source_id=" << state->source_id << " decoder source caps: ";
        gchar *caps_text = caps == nullptr ? nullptr : gst_caps_to_string(caps);
        std::cout << (caps_text == nullptr ? "<unknown>" : caps_text) << '\n';
        g_free(caps_text);
        if (caps != nullptr) {
            gst_caps_unref(caps);
        }
        std::cout << "source_id=" << state->source_id << " first decoded buffer received.\n";
        state->caps_logged = true;
    }
    return GST_PAD_PROBE_OK;
}

void set_display_text(NvDsObjectMeta *object_meta, const std::vector<std::string> &labels,
                      guint source_id) {
    std::ostringstream text;
    text << "cam:" << source_id << ' ' << label_for_object(object_meta, labels) << ' '
         << std::fixed << std::setprecision(2) << object_meta->confidence
         << " ID:" << object_meta->object_id;
    g_free(object_meta->text_params.display_text);
    object_meta->text_params.display_text = g_strdup(text.str().c_str());
}

GstPadProbeReturn metadata_probe(GstPad *, GstPadProbeInfo *info, gpointer user_data) {
    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
        return GST_PAD_PROBE_OK;
    }
    auto *state = static_cast<AppState *>(user_data);
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
        const guint source_id = frame_meta->source_id;

        for (NvDsMetaList *object_list = frame_meta->obj_meta_list;
             object_list != nullptr; object_list = object_list->next) {
            auto *object_meta = static_cast<NvDsObjectMeta *>(object_list->data);
            if (object_meta != nullptr) {
                set_display_text(object_meta, state->labels, source_id);
            }
        }

        if (source_id >= kSourceCount || frame_meta->num_obj_meta == 0 ||
            state->logged_detection_frames[source_id] >= kDetectionFrameLogLimit) {
            continue;
        }

        std::cout << "frame=" << frame_meta->frame_num
                  << " source_id=" << source_id
                  << " objects=" << frame_meta->num_obj_meta << '\n';
        for (NvDsMetaList *object_list = frame_meta->obj_meta_list;
             object_list != nullptr; object_list = object_list->next) {
            auto *object_meta = static_cast<NvDsObjectMeta *>(object_list->data);
            if (object_meta == nullptr) {
                continue;
            }
            std::cout << "  object_id=" << object_meta->object_id
                      << " class_id=" << object_meta->class_id
                      << " label=" << label_for_object(object_meta, state->labels)
                      << " confidence=" << object_meta->confidence << '\n';
        }
        ++state->logged_detection_frames[source_id];
    }
    return GST_PAD_PROBE_OK;
}

bool link_decoder_to_streammux(GstElement *decoder, GstElement *streammux, guint source_id,
                               AppState *state) {
    GstPad *decoder_src_pad = gst_element_get_static_pad(decoder, "src");
    const std::string pad_name = "sink_" + std::to_string(source_id);
    GstPad *streammux_sink_pad = gst_element_request_pad_simple(streammux, pad_name.c_str());
    if (decoder_src_pad == nullptr || streammux_sink_pad == nullptr) {
        if (decoder_src_pad != nullptr) {
            gst_object_unref(decoder_src_pad);
        }
        if (streammux_sink_pad != nullptr) {
            gst_object_unref(streammux_sink_pad);
        }
        return false;
    }
    const GstPadLinkReturn result = gst_pad_link(decoder_src_pad, streammux_sink_pad);
    gst_object_unref(decoder_src_pad);
    if (result != GST_PAD_LINK_OK) {
        gst_element_release_request_pad(streammux, streammux_sink_pad);
        gst_object_unref(streammux_sink_pad);
        return false;
    }
    state->mux_sink_pads[source_id] = streammux_sink_pad;
    return true;
}

void release_streammux_pads(GstElement *streammux, AppState *state) {
    for (guint source_id = 0; source_id < kSourceCount; ++source_id) {
        if (state->mux_sink_pads[source_id] != nullptr) {
            gst_element_release_request_pad(streammux, state->mux_sink_pads[source_id]);
            gst_object_unref(state->mux_sink_pads[source_id]);
            state->mux_sink_pads[source_id] = nullptr;
        }
    }
}

void on_rtsp_pad_added(GstElement *, GstPad *new_pad, gpointer user_data) {
    auto *depay = GST_ELEMENT(user_data);
    GstCaps *caps = current_or_query_caps(new_pad);
    const GstStructure *structure = caps == nullptr ? nullptr : gst_caps_get_structure(caps, 0);
    const gchar *name = structure == nullptr ? nullptr : gst_structure_get_name(structure);
    const gchar *media = structure == nullptr ? nullptr : gst_structure_get_string(structure, "media");
    const gchar *encoding = structure == nullptr ? nullptr : gst_structure_get_string(structure, "encoding-name");
    const bool is_h264_rtp = g_strcmp0(name, "application/x-rtp") == 0 &&
                             g_strcmp0(media, "video") == 0 &&
                             g_strcmp0(encoding, "H264") == 0;
    if (caps != nullptr) {
        gst_caps_unref(caps);
    }
    if (!is_h264_rtp) {
        std::cout << "Source 0: ignoring non-H.264 RTSP pad.\n";
        return;
    }
    GstPad *depay_sink_pad = gst_element_get_static_pad(depay, "sink");
    if (depay_sink_pad == nullptr || gst_pad_is_linked(depay_sink_pad) ||
        gst_pad_link(new_pad, depay_sink_pad) != GST_PAD_LINK_OK) {
        if (depay_sink_pad != nullptr) {
            gst_object_unref(depay_sink_pad);
        }
        std::cerr << "ERROR: could not link Source 0 H.264 RTP pad.\n";
        return;
    }
    gst_object_unref(depay_sink_pad);
    std::cout << "Source 0: RTSP video pad linked.\n";
}

void on_hls_pad_added(GstElement *demux, GstPad *new_pad, gpointer user_data) {
    auto *state = static_cast<HlsLinkState *>(user_data);
    GstCaps *caps = current_or_query_caps(new_pad);
    log_caps("Source 1 hlsdemux output caps: ", caps);
    if (!caps_intersect(caps, "video/mpegts")) {
        if (caps != nullptr) {
            gst_caps_unref(caps);
        }
        post_stream_error(demux, "Source 1 unsupported hlsdemux output; expected video/mpegts");
        return;
    }
    if (caps != nullptr) {
        gst_caps_unref(caps);
    }
    if (state->mpegts_linked) {
        std::cout << "Source 1: ignoring additional MPEG-TS pad.\n";
        return;
    }
    GstPad *sink_pad = gst_element_get_static_pad(state->tsdemux, "sink");
    if (sink_pad == nullptr || gst_pad_link(new_pad, sink_pad) != GST_PAD_LINK_OK) {
        if (sink_pad != nullptr) {
            gst_object_unref(sink_pad);
        }
        post_stream_error(demux, "Could not link Source 1 MPEG-TS to tsdemux");
        return;
    }
    gst_object_unref(sink_pad);
    state->mpegts_linked = true;
    std::cout << "Source 1: HLS MPEG-TS linked.\n";
}

void on_ts_pad_added(GstElement *demux, GstPad *new_pad, gpointer user_data) {
    auto *state = static_cast<HlsLinkState *>(user_data);
    GstCaps *caps = current_or_query_caps(new_pad);
    const GstStructure *structure = caps == nullptr ? nullptr : gst_caps_get_structure(caps, 0);
    const gchar *name = structure == nullptr ? nullptr : gst_structure_get_name(structure);
    log_caps("Source 1 tsdemux output caps: ", caps);
    if (g_strcmp0(name, "video/x-h264") != 0) {
        const bool is_h265 = g_strcmp0(name, "video/x-h265") == 0;
        if (caps != nullptr) {
            gst_caps_unref(caps);
        }
        if (is_h265) {
            post_stream_error(demux, "Source 1 video/x-h265 is unsupported; Stage 5A needs H.264");
        } else {
            std::cout << "Source 1: ignoring non-H.264 TS stream.\n";
        }
        return;
    }
    if (caps != nullptr) {
        gst_caps_unref(caps);
    }
    if (state->h264_linked) {
        std::cout << "Source 1: ignoring additional H.264 video pad.\n";
        return;
    }
    GstPad *sink_pad = gst_element_get_static_pad(state->parser, "sink");
    if (sink_pad == nullptr || gst_pad_link(new_pad, sink_pad) != GST_PAD_LINK_OK) {
        if (sink_pad != nullptr) {
            gst_object_unref(sink_pad);
        }
        post_stream_error(demux, "Could not link Source 1 H.264 to h264parse");
        return;
    }
    gst_object_unref(sink_pad);
    state->h264_linked = true;
    std::cout << "Source 1: HLS H.264 video linked.\n";
}

}  // namespace

int main(int argc, char *argv[]) {
    if (argc != 7) {
        std::cerr << "Usage: " << argv[0] << " <rtsp-url> <hls-url> <nvinfer-config>"
                  << " <labels> <tracker-library> <tracker-config>\n";
        return 2;
    }
    gst_init(&argc, &argv);

    AppState state;
    state.labels = load_labels(argv[4]);
    if (state.labels.empty()) {
        std::cerr << "ERROR: could not read labels.\n";
        return 1;
    }
    DecoderProbeState decoder_states[kSourceCount];
    decoder_states[0].source_id = 0;
    decoder_states[1].source_id = 1;
    HlsLinkState hls_state;
    hls_state.tsdemux = nullptr;
    hls_state.parser = nullptr;

    GstElement *pipeline = gst_pipeline_new("stage5a-multicam-pipeline");
    GstElement *rtsp_source = gst_element_factory_make("rtspsrc", "rtsp-source");
    GstElement *rtsp_depay = gst_element_factory_make("rtph264depay", "rtsp-depay");
    GstElement *rtsp_parser = gst_element_factory_make("h264parse", "rtsp-parser");
    GstElement *rtsp_decoder = gst_element_factory_make("nvv4l2decoder", "rtsp-decoder");
    GstElement *hls_source = gst_element_factory_make("souphttpsrc", "hls-source");
    GstElement *hlsdemux = gst_element_factory_make("hlsdemux", "hlsdemux");
    GstElement *tsdemux = gst_element_factory_make("tsdemux", "tsdemux");
    GstElement *hls_parser = gst_element_factory_make("h264parse", "hls-parser");
    GstElement *hls_capsfilter = gst_element_factory_make("capsfilter", "hls-h264-caps");
    GstElement *hls_decoder = gst_element_factory_make("nvv4l2decoder", "hls-decoder");
    GstElement *streammux = gst_element_factory_make("nvstreammux", "streammux");
    GstElement *infer = gst_element_factory_make("nvinfer", "infer");
    GstElement *tracker = gst_element_factory_make("nvtracker", "tracker");
    GstElement *tiler = gst_element_factory_make("nvmultistreamtiler", "tiler");
    GstElement *convert = gst_element_factory_make("nvvideoconvert", "convert");
    GstElement *rgba_capsfilter = gst_element_factory_make("capsfilter", "rgba-nvmm-caps");
    GstElement *osd = gst_element_factory_make("nvdsosd", "osd");
    GstElement *sink = gst_element_factory_make("nveglglessink", "sink");

    if (pipeline == nullptr || rtsp_source == nullptr || rtsp_depay == nullptr ||
        rtsp_parser == nullptr || rtsp_decoder == nullptr || hls_source == nullptr ||
        hlsdemux == nullptr || tsdemux == nullptr || hls_parser == nullptr ||
        hls_capsfilter == nullptr || hls_decoder == nullptr || streammux == nullptr ||
        infer == nullptr || tracker == nullptr || tiler == nullptr || convert == nullptr ||
        rgba_capsfilter == nullptr || osd == nullptr || sink == nullptr) {
        std::cerr << "ERROR: could not create a required GStreamer element.\n";
        return 1;
    }

    g_object_set(rtsp_source, "location", argv[1], "latency", 200, nullptr);
    gst_util_set_object_arg(G_OBJECT(rtsp_source), "protocols", "tcp");
    g_signal_connect(rtsp_source, "pad-added", G_CALLBACK(on_rtsp_pad_added), rtsp_depay);

    GstStructure *headers = gst_structure_new_empty("headers");
    gst_structure_set(headers, "Referer", G_TYPE_STRING, "https://video.interra.ru/", nullptr);
    g_object_set(hls_source, "location", argv[2],
                 "user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                 "extra-headers", headers, nullptr);
    gst_structure_free(headers);
    hls_state.tsdemux = tsdemux;
    hls_state.parser = hls_parser;
    g_signal_connect(hlsdemux, "pad-added", G_CALLBACK(on_hls_pad_added), &hls_state);
    g_signal_connect(tsdemux, "pad-added", G_CALLBACK(on_ts_pad_added), &hls_state);

    GstCaps *hls_h264_caps = gst_caps_from_string(
        "video/x-h264,stream-format=byte-stream,alignment=au");
    GstCaps *rgba_nvmm_caps = gst_caps_from_string("video/x-raw(memory:NVMM),format=RGBA");
    g_object_set(hls_capsfilter, "caps", hls_h264_caps, nullptr);
    g_object_set(rgba_capsfilter, "caps", rgba_nvmm_caps, nullptr);
    gst_caps_unref(hls_h264_caps);
    gst_caps_unref(rgba_nvmm_caps);
    g_object_set(streammux, "live-source", TRUE, "batch-size", 2, "width", 1280,
                 "height", 720, "batched-push-timeout", 4000000, nullptr);
    g_object_set(infer, "config-file-path", argv[3], nullptr);
    g_object_set(tracker, "tracker-width", 960, "tracker-height", 544, "gpu-id", 0,
                 "ll-lib-file", argv[5], "ll-config-file", argv[6], nullptr);
    g_object_set(tiler, "rows", 1, "columns", 2, "width", 1280, "height", 360, nullptr);

    gst_bin_add_many(GST_BIN(pipeline), rtsp_source, rtsp_depay, rtsp_parser, rtsp_decoder,
                     hls_source, hlsdemux, tsdemux, hls_parser, hls_capsfilter, hls_decoder,
                     streammux, infer, tracker, tiler, convert, rgba_capsfilter, osd, sink,
                     nullptr);
    if (!gst_element_link_many(rtsp_depay, rtsp_parser, rtsp_decoder, nullptr) ||
        !gst_element_link(hls_source, hlsdemux) ||
        !gst_element_link_many(hls_parser, hls_capsfilter, hls_decoder, nullptr) ||
        !link_decoder_to_streammux(rtsp_decoder, streammux, 0, &state) ||
        !link_decoder_to_streammux(hls_decoder, streammux, 1, &state) ||
        !gst_element_link_many(streammux, infer, tracker, tiler, convert, rgba_capsfilter,
                               osd, sink, nullptr)) {
        std::cerr << "ERROR: could not link the pipeline.\n";
        release_streammux_pads(streammux, &state);
        gst_object_unref(pipeline);
        return 1;
    }

    for (guint source_id = 0; source_id < kSourceCount; ++source_id) {
        GstElement *decoder = source_id == 0 ? rtsp_decoder : hls_decoder;
        GstPad *decoder_src_pad = gst_element_get_static_pad(decoder, "src");
        if (decoder_src_pad == nullptr) {
            std::cerr << "ERROR: could not get decoder source pad.\n";
            release_streammux_pads(streammux, &state);
            gst_object_unref(pipeline);
            return 1;
        }
        gst_pad_add_probe(decoder_src_pad, GST_PAD_PROBE_TYPE_BUFFER, decoder_probe,
                          &decoder_states[source_id], nullptr);
        gst_object_unref(decoder_src_pad);
    }
    GstPad *tracker_src_pad = gst_element_get_static_pad(tracker, "src");
    if (tracker_src_pad == nullptr) {
        std::cerr << "ERROR: could not get nvtracker source pad.\n";
        release_streammux_pads(streammux, &state);
        gst_object_unref(pipeline);
        return 1;
    }
    gst_pad_add_probe(tracker_src_pad, GST_PAD_PROBE_TYPE_BUFFER, metadata_probe, &state,
                      nullptr);
    gst_object_unref(tracker_src_pad);

    std::signal(SIGINT, handle_sigint);
    std::cout << "Pipeline: RTSP + HLS -> nvstreammux(batch-size=2) -> nvinfer -> nvtracker"
              << " -> nvmultistreamtiler -> nvvideoconvert -> RGBA/NVMM -> nvdsosd"
              << " -> nveglglessink\n";
    std::cout << "Mux: live-source=1 batch-size=2.\n";
    std::cout << "Inference config: batch-2 engine configured.\n";
    std::cout << "Tracker: NvDCF configured.\n";
    std::cout << "Display: tiler rows=1 columns=2.\n";

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "ERROR: could not set pipeline to PLAYING.\n";
        release_streammux_pads(streammux, &state);
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
            std::cout << "EOS received.\n";
        } else {
            GError *error = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            const gchar *source_name = GST_MESSAGE_SRC(message) == nullptr
                ? "unknown" : GST_OBJECT_NAME(GST_MESSAGE_SRC(message));
            std::cerr << "ERROR from " << source_name << ": "
                      << redact_secret(error == nullptr ? "unknown error" : error->message,
                                       argv[1], argv[2]) << '\n';
            if (debug != nullptr && debug[0] != '\0') {
                std::cerr << "Debug: " << redact_secret(debug, argv[1], argv[2]) << '\n';
            }
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
    release_streammux_pads(streammux, &state);
    std::cout << "Source 0 decoded buffers: " << decoder_states[0].decoded_buffers << '\n';
    std::cout << "Source 1 decoded buffers: " << decoder_states[1].decoded_buffers << '\n';
    gst_object_unref(pipeline);
    return exit_code;
}