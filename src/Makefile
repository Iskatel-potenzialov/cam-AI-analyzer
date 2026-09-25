CXX ?= g++
DEEPSTREAM_INCLUDE_DIR ?= /opt/nvidia/deepstream/deepstream-7.0/sources/includes
DEEPSTREAM_LIB_DIR ?= /opt/nvidia/deepstream/deepstream-7.0/lib

GST_CFLAGS := $(shell pkg-config --cflags gstreamer-1.0)
GST_LIBS := $(shell pkg-config --libs gstreamer-1.0)
CXXFLAGS := -Wall -Wextra -std=c++11 $(GST_CFLAGS) -I$(DEEPSTREAM_INCLUDE_DIR)
LDFLAGS := -L$(DEEPSTREAM_LIB_DIR) -lnvdsgst_meta $(GST_LIBS)
TARGETS := stage2b3d_metadata_probe stage4_rtsp_tracking stage4b_rtsp_display stage5a1_hls_decode stage5a_multicam

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

clean:
	rm -f $(TARGETS)