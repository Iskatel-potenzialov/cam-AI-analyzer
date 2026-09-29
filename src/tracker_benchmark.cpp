#include <gst/gst.h>
#include <glib/gstdio.h>

#include <algorithm>
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

constexpr guint kSourceCount = 5;
constexpr guint kBenchmarkSourceId = 1;
constexpr gint kPersonClassId = 0;
volatile std::sig_atomic_t g_stop = 0;

struct FileSource {
    GstElement *parser = nullptr;
    bool linked = false;
};

struct App {
    std::string variant;
    std::vector<std::string> overlay_lines;
    GstPad *mux_pads[kSourceCount] = {nullptr, nullptr, nullptr, nullptr, nullptr};
};

void on_sigint(int) { g_stop = 1; }

std::vector<std::string> load_overlay_lines(const char *path) {
    std::ifstream input(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

void link_mp4_video(GstElement *demux, GstPad *pad, gpointer user_data) {
    FileSource *source = static_cast<FileSource *>(user_data);
    if (source->linked) return;
    GstCaps *caps = gst_pad_get_current_caps(pad);
    const GstStructure *structure = caps == nullptr ? nullptr : gst_caps_get_structure(caps, 0);
    const gchar *name = structure == nullptr ? nullptr : gst_structure_get_name(structure);
    if (g_strcmp0(name, "video/x-h264") != 0) {
        if (caps != nullptr) gst_caps_unref(caps);
        return;
    }
    if (caps != nullptr) gst_caps_unref(caps);
    GstPad *sink = gst_element_get_static_pad(source->parser, "sink");
    if (sink != nullptr && gst_pad_link(pad, sink) == GST_PAD_LINK_OK) source->linked = true;
    if (sink != nullptr) gst_object_unref(sink);
    if (!source->linked) {
        GError *error = g_error_new_literal(
            GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, "Could not link MP4 H.264 video to parser");
        gst_element_post_message(demux, gst_message_new_error(GST_OBJECT(demux), error, nullptr));
        g_error_free(error);
    }
}

bool link_to_mux(GstElement *decoder, GstElement *mux, guint source_id, GstPad **requested_pad) {
    GstPad *source = gst_element_get_static_pad(decoder, "src");
    const std::string sink_name = "sink_" + std::to_string(source_id);
    GstPad *sink = gst_element_request_pad_simple(mux, sink_name.c_str());
    if (source == nullptr || sink == nullptr) {
        if (source != nullptr) gst_object_unref(source);
        if (sink != nullptr) gst_object_unref(sink);
        return false;
    }
    const bool linked = gst_pad_link(source, sink) == GST_PAD_LINK_OK;
    gst_object_unref(source);
    if (!linked) {
        gst_element_release_request_pad(mux, sink);
        gst_object_unref(sink);
        return false;
    }
    *requested_pad = sink;
    return true;
}

GstPadProbeReturn annotate_source1(GstPad *, GstPadProbeInfo *info, gpointer user_data) {
    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) return GST_PAD_PROBE_OK;
    App *app = static_cast<App *>(user_data);
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    NvDsBatchMeta *batch = gst_buffer_get_nvds_batch_meta(buffer);
    if (batch == nullptr) return GST_PAD_PROBE_OK;

    for (NvDsMetaList *frame_node = batch->frame_meta_list; frame_node != nullptr;
         frame_node = frame_node->next) {
        NvDsFrameMeta *frame = static_cast<NvDsFrameMeta *>(frame_node->data);
        if (frame == nullptr || frame->source_id != kBenchmarkSourceId) continue;

        for (NvDsMetaList *object_node = frame->obj_meta_list; object_node != nullptr;) {
            NvDsMetaList *next = object_node->next;
            NvDsObjectMeta *object = static_cast<NvDsObjectMeta *>(object_node->data);
            if (object == nullptr || object->class_id != kPersonClassId) {
                if (object != nullptr) nvds_remove_obj_meta_from_frame(frame, object);
                object_node = next;
                continue;
            }
            std::ostringstream label;
            label << "ID:" << object->object_id << " " << std::fixed << std::setprecision(2)
                  << object->confidence;
            g_free(object->text_params.display_text);
            object->text_params.display_text = g_strdup(label.str().c_str());
            object->text_params.set_bg_clr = 1;
            object->text_params.text_bg_clr.red = 0.0f;
            object->text_params.text_bg_clr.green = 0.0f;
            object->text_params.text_bg_clr.blue = 0.0f;
            object->text_params.text_bg_clr.alpha = 0.7f;
            object->text_params.font_params.font_name = const_cast<gchar *>("Sans");
            object->text_params.font_params.font_size = 30;
            object->text_params.font_params.font_color.red = 1.0f;
            object->text_params.font_params.font_color.green = 1.0f;
            object->text_params.font_params.font_color.blue = 1.0f;
            object->text_params.font_params.font_color.alpha = 1.0f;
            object->rect_params.border_width = 3;
            object->rect_params.border_color.red = 0.0f;
            object->rect_params.border_color.green = 1.0f;
            object->rect_params.border_color.blue = 0.0f;
            object->rect_params.border_color.alpha = 1.0f;
            object_node = next;
        }

        NvDsDisplayMeta *display = nvds_acquire_display_meta_from_pool(batch);
        if (display == nullptr) continue;
        constexpr gint kCardLeft = 20;
        constexpr gint kCardTop = 20;
        constexpr gint kCardPadding = 20;
        constexpr gint kTitleFontSize = 48;
        constexpr gint kParameterFontSize = 30;
        constexpr gint kTitleLineHeight = 60;
        constexpr gint kTitleParameterGap = 28;
        constexpr gint kParameterLineHeight = 50;
        const guint label_count =
            static_cast<guint>(std::min<size_t>(app->overlay_lines.size(), 9));
        const guint parameter_count = label_count > 0 ? label_count - 1 : 0;
        size_t max_parameter_chars = 0;
        for (guint index = 1; index < label_count; ++index) {
            max_parameter_chars = std::max(max_parameter_chars, app->overlay_lines[index].size());
        }
        display->num_rects = 1;
        NvOSD_RectParams &card = display->rect_params[0];
        card.left = kCardLeft;
        card.top = kCardTop;
        card.width = std::max(kTitleFontSize, static_cast<gint>(max_parameter_chars * 18)) +
                     2 * kCardPadding;
        card.height = 2 * kCardPadding + kTitleLineHeight + kTitleParameterGap +
                      static_cast<gint>(parameter_count) * kParameterLineHeight;
        card.border_width = 0;
        card.has_bg_color = 1;
        card.bg_color.red = 0.0f;
        card.bg_color.green = 0.0f;
        card.bg_color.blue = 0.0f;
        card.bg_color.alpha = 0.75f;
        display->num_labels = label_count;
        for (guint index = 0; index < label_count; ++index) {
            NvOSD_TextParams &text = display->text_params[index];
            text.display_text = g_strdup(app->overlay_lines[index].c_str());
            text.x_offset = kCardLeft + kCardPadding;
            text.y_offset = index == 0
                                ? kCardTop + kCardPadding
                                : kCardTop + kCardPadding + kTitleLineHeight +
                                      kTitleParameterGap +
                                      static_cast<gint>((index - 1) * kParameterLineHeight);
            text.set_bg_clr = 0;
            text.font_params.font_name = const_cast<gchar *>("Sans");
            text.font_params.font_size = index == 0 ? kTitleFontSize : kParameterFontSize;
            text.font_params.font_color.red = 1.0f;
            text.font_params.font_color.green = 1.0f;
            text.font_params.font_color.blue = 1.0f;
            text.font_params.font_color.alpha = 1.0f;
        }
        nvds_add_display_meta_to_frame(frame, display);
    }
    return GST_PAD_PROBE_OK;
}

void release_pad(GstElement *element, GstPad *pad) {
    if (pad != nullptr) {
        gst_element_release_request_pad(element, pad);
        gst_object_unref(pad);
    }
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 10) {
        std::cerr << "Usage: " << argv[0]
                  << " <clip.mp4> <output.mp4> <A|B|C|D> <nvinfer-config> <labels>"
                  << " <tracker-lib> <tracker-config> <preprocess-config> <overlay-text-file>\n";
        return 2;
    }
    const char *clip = argv[1];
    const char *output = argv[2];
    App app;
    app.variant = argv[3];
    app.overlay_lines = load_overlay_lines(argv[9]);
    if (app.variant.size() != 1 || app.variant.find_first_not_of("ABCD") != std::string::npos ||
        app.overlay_lines.empty()) {
        std::cerr << "Variant must be A, B, C, or D.\n";
        return 2;
    }

    gst_init(&argc, &argv);
    GstElement *pipeline = gst_pipeline_new("tracker_benchmark");
    GstElement *mux = gst_element_factory_make("nvstreammux", "mux");
    GstElement *preprocess = gst_element_factory_make("nvdspreprocess", "preprocess");
    GstElement *infer = gst_element_factory_make("nvinfer", "infer");
    GstElement *tracker = gst_element_factory_make("nvtracker", "tracker");
    GstElement *demux = gst_element_factory_make("nvstreamdemux", "demux");
    GstElement *output_queue = gst_element_factory_make("queue", "source1_queue");
    GstElement *rgba_convert = gst_element_factory_make("nvvideoconvert", "rgba_convert");
    GstElement *rgba_caps = gst_element_factory_make("capsfilter", "rgba_caps");
    GstElement *osd = gst_element_factory_make("nvdsosd", "osd");
    GstElement *i420_convert = gst_element_factory_make("nvvideoconvert", "i420_convert");
    GstElement *i420_caps = gst_element_factory_make("capsfilter", "i420_caps");
    GstElement *encoder = gst_element_factory_make("nvv4l2h264enc", "encoder");
    GstElement *parser = gst_element_factory_make("h264parse", "output_parser");
    GstElement *qtmux = gst_element_factory_make("qtmux", "qtmux");
    GstElement *sink = gst_element_factory_make("filesink", "sink");
    if (pipeline == nullptr || mux == nullptr || preprocess == nullptr || infer == nullptr ||
        tracker == nullptr || demux == nullptr || output_queue == nullptr || rgba_convert == nullptr ||
        rgba_caps == nullptr || osd == nullptr || i420_convert == nullptr || i420_caps == nullptr ||
        encoder == nullptr || parser == nullptr || qtmux == nullptr || sink == nullptr) {
        std::cerr << "Could not create benchmark GStreamer elements.\n";
        return 1;
    }

    g_object_set(mux, "live-source", FALSE, "batch-size", kSourceCount, "width", 2560,
                 "height", 1440, "batched-push-timeout", 4000000, nullptr);
    g_object_set(preprocess, "config-file", argv[8], nullptr);
    g_object_set(infer, "config-file-path", argv[4], "input-tensor-meta", TRUE, nullptr);
    g_object_set(tracker, "tracker-width", 960, "tracker-height", 544, "gpu-id", 0,
                 "ll-lib-file", argv[6], "ll-config-file", argv[7], nullptr);
    g_object_set(encoder, "bitrate", 8000000, nullptr);
    g_object_set(qtmux, "faststart", TRUE, nullptr);
    g_object_set(sink, "location", output, "sync", FALSE, nullptr);
    GstCaps *rgba = gst_caps_from_string("video/x-raw(memory:NVMM),format=RGBA");
    GstCaps *i420 = gst_caps_from_string("video/x-raw(memory:NVMM),format=I420");
    g_object_set(rgba_caps, "caps", rgba, nullptr);
    g_object_set(i420_caps, "caps", i420, nullptr);
    gst_caps_unref(rgba);
    gst_caps_unref(i420);

    gst_bin_add_many(GST_BIN(pipeline), mux, preprocess, infer, tracker, demux, output_queue,
                     rgba_convert, rgba_caps, osd, i420_convert, i420_caps, encoder, parser,
                     qtmux, sink, nullptr);
    if (!gst_element_link_many(mux, preprocess, infer, tracker, demux, nullptr) ||
        !gst_element_link_many(output_queue, rgba_convert, rgba_caps, osd, i420_convert,
                               i420_caps, encoder, parser, qtmux, sink, nullptr)) {
        std::cerr << "Could not link benchmark pipeline.\n";
        return 1;
    }

    FileSource file_sources[kSourceCount];
    for (guint index = 0; index < kSourceCount; ++index) {
        GstElement *filesrc = gst_element_factory_make("filesrc", nullptr);
        GstElement *qtdemux = gst_element_factory_make("qtdemux", nullptr);
        GstElement *h264parse = gst_element_factory_make("h264parse", nullptr);
        GstElement *decoder = gst_element_factory_make("nvv4l2decoder", nullptr);
        if (filesrc == nullptr || qtdemux == nullptr || h264parse == nullptr || decoder == nullptr) {
            std::cerr << "Could not create input branch " << index << ".\n";
            return 1;
        }
        file_sources[index].parser = h264parse;
        g_object_set(filesrc, "location", clip, nullptr);
        g_signal_connect(qtdemux, "pad-added", G_CALLBACK(link_mp4_video), &file_sources[index]);
        gst_bin_add_many(GST_BIN(pipeline), filesrc, qtdemux, h264parse, decoder, nullptr);
        if (!gst_element_link(filesrc, qtdemux) || !gst_element_link(h264parse, decoder) ||
            !link_to_mux(decoder, mux, index, &app.mux_pads[index])) {
            std::cerr << "Could not link input branch " << index << ".\n";
            return 1;
        }
    }

    GstPad *demux_source = gst_element_request_pad_simple(demux, "src_1");
    GstPad *output_sink = gst_element_get_static_pad(output_queue, "sink");
    const bool demux_linked = demux_source != nullptr && output_sink != nullptr &&
                              gst_pad_link(demux_source, output_sink) == GST_PAD_LINK_OK;
    if (output_sink != nullptr) gst_object_unref(output_sink);
    if (!demux_linked) {
        if (demux_source != nullptr) gst_object_unref(demux_source);
        std::cerr << "Could not link benchmark source1 output.\n";
        return 1;
    }

    GstPad *tracker_src = gst_element_get_static_pad(tracker, "src");
    gst_pad_add_probe(tracker_src, GST_PAD_PROBE_TYPE_BUFFER, annotate_source1, &app, nullptr);
    gst_object_unref(tracker_src);

    std::signal(SIGINT, on_sigint);
    std::cout << "TRACKER_BENCHMARK_START variant=" << app.variant << " clip=" << clip
              << " output=" << output << " source_id=1 batch_size=5\n";
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Could not start benchmark pipeline.\n";
        return 1;
    }

    GstBus *bus = gst_element_get_bus(pipeline);
    int exit_code = 0;
    while (!g_stop) {
        GstMessage *message = gst_bus_timed_pop_filtered(
            bus, 100 * GST_MSECOND, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (message == nullptr) continue;
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError *error = nullptr;
            gchar *detail = nullptr;
            gst_message_parse_error(message, &error, &detail);
            std::cerr << "ERROR from " << GST_OBJECT_NAME(GST_MESSAGE_SRC(message)) << ": "
                      << (error == nullptr ? "unknown" : error->message) << "\n";
            g_clear_error(&error);
            g_free(detail);
            exit_code = 1;
        }
        gst_message_unref(message);
        break;
    }
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_element_get_state(pipeline, nullptr, nullptr, GST_CLOCK_TIME_NONE);
    for (guint index = 0; index < kSourceCount; ++index) release_pad(mux, app.mux_pads[index]);
    release_pad(demux, demux_source);
    gst_object_unref(pipeline);
    std::cout << "TRACKER_BENCHMARK_DONE variant=" << app.variant
              << " status=" << (exit_code == 0 ? "ok" : "error") << "\n";
    return exit_code;
}
