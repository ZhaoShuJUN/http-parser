# GC6511 Buildroot SDK provides target-specific compiler and libmosquitto libraries.
CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc
PKG_CONFIG ?= pkg-config
CPPFLAGS += -D_DEFAULT_SOURCE
CFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
CFLAGS += -pthread
LDFLAGS ?=
# If target SDK has no pkg-config metadata: make MOSQ_CFLAGS='-I...' MOSQ_LIBS='-L... -lmosquitto'
MOSQ_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libmosquitto 2>/dev/null)
MOSQ_LIBS ?= $(shell $(PKG_CONFIG) --libs libmosquitto 2>/dev/null || echo -lmosquitto)
LDLIBS += $(MOSQ_LIBS) -pthread
TARGET := iot_gateway
SOURCES := src/main.c src/auth.c src/net.c src/json.c
OBJECTS := $(SOURCES:.c=.o)
.PHONY: all clean test test-mock
all: $(TARGET)
$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
src/%.o: src/%.c src/gateway.h src/auth.h src/net.h src/json.h
	$(CC) $(CPPFLAGS) $(MOSQ_CFLAGS) $(CFLAGS) -Isrc -c -o $@ $<
clean:
	rm -f $(OBJECTS) $(TARGET)
test: $(TARGET)
	python3 tests/test_gateway.py

# Offline test build: intentionally does NOT use libmosquitto. Never deploy it.
test-mock:
	$(CC) -D_DEFAULT_SOURCE $(CFLAGS) -Itests/mock -Isrc $(SOURCES) tests/mock/mock_mosquitto.c -o tests/iot_gateway_mock -pthread
	python3 tests/test_gateway.py --exe tests/iot_gateway_mock
	rm -f tests/iot_gateway_mock
