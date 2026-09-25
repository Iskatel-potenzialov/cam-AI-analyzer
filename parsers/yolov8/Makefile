CXX ?= g++
DEEPSTREAM_INCLUDE_DIR ?= /opt/nvidia/deepstream/deepstream-7.0/sources/includes

CXXFLAGS := -Wall -Wextra -std=c++11 -fPIC -Wno-error=deprecated-declarations
TARGET_LIB := libnvdsinfer_custom_impl_Yolo.so
SOURCE := nvdsparsebbox_Yolo.cpp

all: $(TARGET_LIB)

$(TARGET_LIB): $(SOURCE) Makefile
	$(CXX) $(CXXFLAGS) -I$(DEEPSTREAM_INCLUDE_DIR) -shared -o $@ $(SOURCE)

clean:
	rm -f $(TARGET_LIB)