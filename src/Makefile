CXX ?= g++
DEEPSTREAM_INCLUDE_DIR ?= /opt/nvidia/deepstream/deepstream-7.0/sources/includes
DEEPSTREAM_LIB_DIR ?= /opt/nvidia/deepstream/deepstream-7.0/lib

GST_CFLAGS := $(shell pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0)
GST_LIBS := $(shell pkg-config --libs gstreamer-1.0 gstreamer-app-1.0)
CURL_CFLAGS := $(shell pkg-config --cflags libcurl)
CURL_LIBS := $(shell pkg-config --libs libcurl)
CXXFLAGS := -Wall -Wextra -std=c++11 $(GST_CFLAGS) -I$(DEEPSTREAM_INCLUDE_DIR)
LDFLAGS := -L$(DEEPSTREAM_LIB_DIR) -lnvdsgst_meta -lnvds_meta $(GST_LIBS)
TARGETS := stage2b3d_metadata_probe stage4_rtsp_tracking stage4b_rtsp_display stage5a1_hls_decode stage5a_multicam stage5b_5cam

all: $(TARGETS)

stage2b3d_metadata_probe: stage2b3d_metadata_probe.cpp Makefile
	$(CXX) $(CXXFLAGS) -o $@ stage2b3d_metadata_probe.cpp $(LDFLAGS)

stage4_rtsp_tracking: stage4_rtsp_tracking.cpp Makefile
	$(CXX) $(CXXFLAGS) -o $@ stage4_rtsp_tracking.cpp $(LDFLAGS)

stage4b_rtsp_display: stage4b_rtsp_display.cpp Makefile
	$(CXX) $(CXXFLAGS) -o $@ stage4b_rtsp_display.cpp $(LDFLAGS)

stage5a1_hls_decode: stage5a1_hls_decode.cpp Makefile
	$(CXX) -Wall -Wextra -std=c++11 $(GST_CFLAGS) -o $@ stage5a1_hls_decode.cpp $(GST_LIBS)

stage5a_multicam: stage5a_multicam.cpp Makefile
	$(CXX) $(CXXFLAGS) -o $@ stage5a_multicam.cpp $(LDFLAGS)

stage5b_5cam: stage5b_5cam.cpp event_sender.cpp event_sender.h live_state_sender.cpp live_state_sender.h Makefile
	$(CXX) $(CXXFLAGS) $(CURL_CFLAGS) -o $@ stage5b_5cam.cpp event_sender.cpp live_state_sender.cpp $(LDFLAGS) $(CURL_LIBS)

event_sender_test: event_sender_test.cpp event_sender.cpp event_sender.h Makefile
	$(CXX) -Wall -Wextra -std=c++11 -pthread $(CURL_CFLAGS) -o $@ event_sender_test.cpp event_sender.cpp $(CURL_LIBS)

live_state_sender_test: live_state_sender_test.cpp live_state_sender.cpp live_state_sender.h Makefile
	$(CXX) -Wall -Wextra -std=c++11 -pthread $(CURL_CFLAGS) -o $@ live_state_sender_test.cpp live_state_sender.cpp $(CURL_LIBS)

clean:
	rm -f $(TARGETS) event_sender_test live_state_sender_test