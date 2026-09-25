#include <gst/gst.h>

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

constexpr guint kProgressIntervalBuffers = 300;
volatile std::sig_atomic_t g_interrupted = 0;

struct AppState {
    guint64 decoded_buffers = 0;
    bool decoder_caps_logged = false;
    bool hls_pad_linked = false;
    bool ts_video_pad_linked = false;
};

void handle_sigint(int) {
    g_interrupted = 1;
}

void post_stream_error(GstElement *element, const std::string &message) {
    GError *error = g_error_new_literal(GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED,
                                        message.c_str());
    GstMessage *bus_message = gst_message_new_error(GST_OBJECT(element), error, nullptr);
    g_error_free(error);
    gst_element_post_message(element, bus_message);
}

std::string redact_url(const gchar *text, const char *url) {
    std::string result = text == nullptr ? "" : text;
    const std::string secret = url == nullptr ? "" : url;
    if (secret.empty()) {
        return result;
    }

    std::string::size_type position = 0;
    while ((position = result.find(secret, position)) != std::string::npos) {
        result.replace(position, secret.size(), "<redacted-hls-url>");
        position += sizeof("<redacted-hls-url>") - 1;
    }
    return result;
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

GstPadProbeReturn decoded_buffer_probe(GstPad *pad, GstPadProbeInfo *info,
                                       gpointer user_data) {
    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
        return GST_PAD_PROBE_OK;
    }

    auto *state = static_cast<AppState *>(user_data);
    ++state->decoded_buffers;

    if (!state->decoder_caps_logged) {
        GstCaps *caps = current_or_query_caps(pad);
        log_caps("Decoder source caps: ", caps);
        if (caps != nullptr) {
            gst_caps_unref(caps);
        }
        std::cout << "First decoded buffer received.\n";
        state->decoder_caps_logged = true;
    } else if (state->decoded_buffers % kProgressIntervalBuffers == 0) {
        std::cout << "Decoded buffers: " << state->decoded_buffers << '\n';
    }

    return GST_PAD_PROBE_OK;
}

void on_hls_pad_added(GstElement *demux, GstPad *new_pad, gpointer user_data) {
    auto *state = static_cast<AppState *>(user_data);
    GstCaps *caps = current_or_query_caps(new_pad);
    log_caps("hlsdemux output caps: ", caps);

    if (!caps_intersect(caps, "video/mpegts")) {
        if (caps != nullptr) {
            gst_caps_unref(caps);
        }
        post_stream_error(demux,
                          "Unsupported hlsdemux output: expected video/mpegts for Stage 5A.1");
        return;
    }

    if (caps != nullptr) {
        gst_caps_unref(caps);
    }

    if (state->hls_pad_linked) {
        std::cout << "Ignoring additional MPEG-TS pad from hlsdemux.\n";
        return;
    }

    GstElement *tsdemux = GST_ELEMENT(g_object_get_data(G_OBJECT(demux), "tsdemux"));
    GstPad *tsdemux_sink_pad = tsdemux == nullptr
        ? nullptr
        : gst_element_get_static_pad(tsdemux, "sink");
    if (tsdemux_sink_pad == nullptr || gst_pad_link(new_pad, tsdemux_sink_pad) != GST_PAD_LINK_OK) {
        if (tsdemux_sink_pad != nullptr) {
            gst_object_unref(tsdemux_sink_pad);
        }
        post_stream_error(demux, "Could not link MPEG-TS from hlsdemux to tsdemux");
        return;
    }

    gst_object_unref(tsdemux_sink_pad);
    state->hls_pad_linked = true;
    std::cout << "Linked MPEG-TS from hlsdemux to tsdemux.\n";
}

void on_ts_pad_added(GstElement *demux, GstPad *new_pad, gpointer user_data) {
    auto *state = static_cast<AppState *>(user_data);
    GstCaps *caps = current_or_query_caps(new_pad);
    const GstStructure *structure = caps == nullptr ? nullptr : gst_caps_get_structure(caps, 0);
    const gchar *name = structure == nullptr ? nullptr : gst_structure_get_name(structure);
    log_caps("tsdemux output caps: ", caps);

    if (g_strcmp0(name, "video/x-h264") != 0) {
        const bool is_h265 = g_strcmp0(name, "video/x-h265") == 0;
        if (caps != nullptr) {
            gst_caps_unref(caps);
        }
        if (is_h265) {
            post_stream_error(demux,
                              "Unsupported source codec video/x-h265; Stage 5A.1 supports H.264 only");
        } else {
            std::cout << "Ignoring non-H.264 stream from tsdemux.\n";
        }
        return;
    }

    if (caps != nullptr) {
        gst_caps_unref(caps);
    }

    if (state->ts_video_pad_linked) {
        std::cout << "Ignoring additional H.264 video pad from tsdemux.\n";
        return;
    }

    GstElement *parser = GST_ELEMENT(g_object_get_data(G_OBJECT(demux), "parser"));
    GstPad *parser_sink_pad = parser == nullptr
        ? nullptr
        : gst_element_get_static_pad(parser, "sink");
    if (parser_sink_pad == nullptr || gst_pad_link(new_pad, parser_sink_pad) != GST_PAD_LINK_OK) {
        if (parser_sink_pad != nullptr) {
            gst_object_unref(parser_sink_pad);
        }
        post_stream_error(demux, "Could not link H.264 from tsdemux to h264parse");
        return;
    }

    gst_object_unref(parser_sink_pad);
    state->ts_video_pad_linked = true;
    std::cout << "Linked H.264 video from tsdemux to h264parse.\n";
}

bool parse_duration(const char *text, guint *duration_seconds) {
    char *end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (text[0] == '\0' || end == nullptr || *end != '\0' || parsed <= 0 ||
        parsed > G_MAXUINT) {
        return false;
    }
    *duration_seconds = static_cast<guint>(parsed);
    return true;
}

}  // namespace

int main(int argc, char *argv[]) {
    if (argc != 2 && argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <hls-url> [duration-seconds]\n";
        return 2;
    }

    guint duration_seconds = 0;
    if (argc == 3 && !parse_duration(argv[2], &duration_seconds)) {
        std::cerr << "ERROR: duration-seconds must be a positive integer\n";
        return 2;
    }

    gst_init(&argc, &argv);

    AppState state;
    GstElement *pipeline = gst_pipeline_new("stage5a1-hls-decode-pipeline");
    GstElement *source = gst_element_factory_make("souphttpsrc", "source");
    GstElement *hlsdemux = gst_element_factory_make("hlsdemux", "hlsdemux");
    GstElement *tsdemux = gst_element_factory_make("tsdemux", "tsdemux");
    GstElement *parser = gst_element_factory_make("h264parse", "parser");
    GstElement *h264_capsfilter = gst_element_factory_make("capsfilter", "h264-caps");
    GstElement *decoder = gst_element_factory_make("nvv4l2decoder", "decoder");
    GstElement *sink = gst_element_factory_make("fakesink", "sink");

    if (pipeline == nullptr || source == nullptr || hlsdemux == nullptr || tsdemux == nullptr ||
        parser == nullptr || h264_capsfilter == nullptr || decoder == nullptr || sink == nullptr) {
        std::cerr << "ERROR: could not create a required GStreamer element\n";
        return 1;
    }

    GstStructure *headers = gst_structure_new_empty("headers");
    gst_structure_set(headers, "Referer", G_TYPE_STRING, "https://video.interra.ru/", nullptr);
    g_object_set(source, "location", argv[1],
                 "user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                 "extra-headers", headers, nullptr);
    gst_structure_free(headers);

    GstCaps *h264_caps = gst_caps_from_string(
        "video/x-h264,stream-format=byte-stream,alignment=au");
    g_object_set(h264_capsfilter, "caps", h264_caps, nullptr);
    gst_caps_unref(h264_caps);
    g_object_set(sink, "sync", FALSE, nullptr);

    g_object_set_data(G_OBJECT(hlsdemux), "tsdemux", tsdemux);
    g_object_set_data(G_OBJECT(tsdemux), "parser", parser);
    g_signal_connect(hlsdemux, "pad-added", G_CALLBACK(on_hls_pad_added), &state);
    g_signal_connect(tsdemux, "pad-added", G_CALLBACK(on_ts_pad_added), &state);

    gst_bin_add_many(GST_BIN(pipeline), source, hlsdemux, tsdemux, parser, h264_capsfilter,
                     decoder, sink, nullptr);
    if (!gst_element_link(source, hlsdemux) ||
        !gst_element_link_many(parser, h264_capsfilter, decoder, sink, nullptr)) {
        std::cerr << "ERROR: could not link the static pipeline\n";
        gst_object_unref(pipeline);
        return 1;
    }

    GstPad *decoder_src_pad = gst_element_get_static_pad(decoder, "src");
    if (decoder_src_pad == nullptr) {
        std::cerr << "ERROR: could not get nvv4l2decoder source pad\n";
        gst_object_unref(pipeline);
        return 1;
    }
    gst_pad_add_probe(decoder_src_pad, GST_PAD_PROBE_TYPE_BUFFER, decoded_buffer_probe,
                      &state, nullptr);
    gst_object_unref(decoder_src_pad);

    std::signal(SIGINT, handle_sigint);
    std::cout << "Pipeline: souphttpsrc -> hlsdemux -> tsdemux -> h264parse"
              << " -> capsfilter(video/x-h264,stream-format=byte-stream,alignment=au)"
              << " -> nvv4l2decoder -> fakesink\n";
    std::cout << "HTTP headers configured: User-Agent and Referer.\n";
    if (duration_seconds == 0) {
        std::cout << "Duration: until Ctrl+C.\n";
    } else {
        std::cout << "Duration: " << duration_seconds << " seconds.\n";
    }

    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "ERROR: could not set pipeline to PLAYING\n";
        gst_object_unref(pipeline);
        return 1;
    }

    GstBus *bus = gst_element_get_bus(pipeline);
    const gint64 started_at = g_get_monotonic_time();
    int exit_code = 0;
    bool done = false;

    while (!done && g_interrupted == 0) {
        GstMessage *message = gst_bus_timed_pop_filtered(
            bus, 100 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (message != nullptr) {
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
                std::cout << "EOS received from live HLS pipeline.\n";
            } else {
                GError *error = nullptr;
                gchar *debug = nullptr;
                gst_message_parse_error(message, &error, &debug);
                const gchar *source_name = GST_MESSAGE_SRC(message) == nullptr
                    ? "unknown"
                    : GST_OBJECT_NAME(GST_MESSAGE_SRC(message));
                std::cerr << "ERROR from " << source_name << ": "
                          << redact_url(error == nullptr ? "unknown error" : error->message,
                                        argv[1]) << '\n';
                if (debug != nullptr && debug[0] != '\0') {
                    std::cerr << "Debug: " << redact_url(debug, argv[1]) << '\n';
                }
                g_clear_error(&error);
                g_free(debug);
                exit_code = 1;
            }
            gst_message_unref(message);
            done = true;
            continue;
        }

        if (duration_seconds != 0 &&
            g_get_monotonic_time() - started_at >=
                static_cast<gint64>(duration_seconds) * G_USEC_PER_SEC) {
            std::cout << "Duration reached; stopping pipeline.\n";
            done = true;
        }
    }

    if (g_interrupted != 0) {
        std::cout << "SIGINT received; stopping pipeline.\n";
    }

    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_element_get_state(pipeline, nullptr, nullptr, GST_CLOCK_TIME_NONE);
    std::cout << "Decoded buffers total: " << state.decoded_buffers << '\n';
    gst_object_unref(pipeline);
    return exit_code;
}