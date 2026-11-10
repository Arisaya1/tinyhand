# TinyHand for Windows.
# Cross-compile from macOS or Linux with mingw-w64, or build natively in MSYS2.
# Production and test executables are deliberately separate.

CROSS ?= x86_64-w64-mingw32-
CC := $(CROSS)gcc
CXX := $(CROSS)g++
WINDRES := $(CROSS)windres

comma := ,
VERSION := $(shell tr -d '[:space:]' < ../VERSION)
VERSION_NUM := $(subst .,$(comma),$(VERSION))$(comma)0
BUILD := build
NAME := TinyHand-$(VERSION)-windows-x64
DIST := dist/$(NAME)

CPPFLAGS := -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -I../Sources/ShapeKit/include -Isrc -I$(BUILD)
CFLAGS := -std=c99 -O2 -Wall -Wextra
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -municode
LDFLAGS := -municode -mwindows -static -static-libgcc -static-libstdc++ -s
LDLIBS := -lgdiplus -lcomctl32 -lcomdlg32 -lshlwapi -lshell32 -lole32 -luuid -lwinmm -lwinhttp -lbcrypt -lgdi32 -luser32 -ladvapi32

PROD_SOURCES := $(filter-out src/selftest.cpp src/updater_main.cpp,$(wildcard src/*.cpp))
HEADERS := $(wildcard src/*.h) ../Sources/ShapeKit/include/shapekit.h
OBJECTS := $(PROD_SOURCES:src/%.cpp=$(BUILD)/%.o) $(BUILD)/shapekit.o $(BUILD)/tinyhand.res.o
TEST_OBJECTS := $(filter-out $(BUILD)/main.o,$(OBJECTS)) $(BUILD)/test-main.o $(BUILD)/selftest.o

.PHONY: all test check dist clean
all: $(BUILD)/TinyHand.exe $(BUILD)/TinyHandUpdater.exe

$(BUILD)/TinyHandUpdater.exe: src/updater_main.cpp src/update.cpp src/update.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -static -static-libgcc -static-libstdc++ -s -o $@ src/updater_main.cpp src/update.cpp -lwinhttp -lbcrypt

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/version.h: ../VERSION | $(BUILD)
	printf '#pragma once\n#define TH_VERSION L"%s"\n#define TH_VERSION_A "%s"\n#define TH_VERSION_NUM %s\n' \
		'$(VERSION)' '$(VERSION)' '$(VERSION_NUM)' > $@

$(BUILD)/%.o: src/%.cpp $(HEADERS) $(BUILD)/version.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/test-main.o: src/main.cpp $(HEADERS) $(BUILD)/version.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DTH_ENABLE_SELFTEST -c $< -o $@

$(BUILD)/shapekit.o: ../Sources/ShapeKit/shapekit.c ../Sources/ShapeKit/include/shapekit.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/tinyhand.res.o: res/tinyhand.rc res/tinyhand.manifest res/tinyhand.ico res/appicon.png src/resource.h $(BUILD)/version.h
	$(WINDRES) -I$(BUILD) -Isrc -O coff $< $@

$(BUILD)/TinyHand.exe: $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/TinyHand-test.exe: $(TEST_OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: $(BUILD)/shape_test.exe

check: test $(BUILD)/TinyHand-test.exe

$(BUILD)/shape_test.exe: ../Tests/shapes/shape_test.c ../Sources/ShapeKit/shapekit.c ../Sources/ShapeKit/include/shapekit.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -static -o $@ ../Tests/shapes/shape_test.c ../Sources/ShapeKit/shapekit.c

dist: all
	rm -rf $(DIST) dist/$(NAME).zip
	mkdir -p $(DIST)/assets
	cp $(BUILD)/TinyHand.exe $(BUILD)/TinyHandUpdater.exe $(DIST)/
	cp ../VERSION $(DIST)/
	cp ../assets/hand_*.png $(DIST)/assets/
	cp ../LICENSE $(DIST)/LICENSE.txt
	cd $(DIST) && zip -qr ../$(NAME).zip .
	@echo "Built dist/$(NAME).zip"

clean:
	rm -rf $(BUILD) dist
