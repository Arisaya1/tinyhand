# TinyHand for Windows.
#
# Cross-compile from macOS or Linux with mingw-w64 (brew install mingw-w64,
# apt install mingw-w64):            make
# Build natively in an MSYS2 MinGW64 shell:  make CROSS=
#
# Targets: all (build/TinyHand.exe), test (build/shape_test.exe), dist (zip), clean.

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
LDLIBS := -lgdiplus -lcomctl32 -lshlwapi -lshell32 -lole32 -luuid -lwinmm -lgdi32 -luser32 -ladvapi32

SOURCES := $(wildcard src/*.cpp)
HEADERS := $(wildcard src/*.h) ../Sources/ShapeKit/include/shapekit.h
OBJECTS := $(SOURCES:src/%.cpp=$(BUILD)/%.o) $(BUILD)/shapekit.o $(BUILD)/tinyhand.res.o

.PHONY: all test dist clean

all: $(BUILD)/TinyHand.exe

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/version.h: ../VERSION | $(BUILD)
	printf '#pragma once\n#define TH_VERSION L"%s"\n#define TH_VERSION_A "%s"\n#define TH_VERSION_NUM %s\n' \
		'$(VERSION)' '$(VERSION)' '$(VERSION_NUM)' > $@

$(BUILD)/%.o: src/%.cpp $(HEADERS) $(BUILD)/version.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/shapekit.o: ../Sources/ShapeKit/shapekit.c ../Sources/ShapeKit/include/shapekit.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/tinyhand.res.o: res/tinyhand.rc res/tinyhand.manifest res/tinyhand.ico res/appicon.png src/resource.h $(BUILD)/version.h
	$(WINDRES) -I$(BUILD) -Isrc -O coff $< $@

$(BUILD)/TinyHand.exe: $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: $(BUILD)/shape_test.exe

$(BUILD)/shape_test.exe: ../Tests/shapes/shape_test.c ../Sources/ShapeKit/shapekit.c ../Sources/ShapeKit/include/shapekit.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -static -o $@ ../Tests/shapes/shape_test.c ../Sources/ShapeKit/shapekit.c

dist: all
	rm -rf $(DIST) dist/$(NAME).zip
	mkdir -p $(DIST)/assets
	cp $(BUILD)/TinyHand.exe $(DIST)/
	cp ../assets/hand_*.png $(DIST)/assets/
	cp ../LICENSE $(DIST)/LICENSE.txt
	cd dist && zip -qr $(NAME).zip $(NAME)
	@echo "Built dist/$(NAME).zip"

clean:
	rm -rf $(BUILD) dist
