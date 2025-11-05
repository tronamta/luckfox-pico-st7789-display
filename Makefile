# RV1103 cross-compiler
CC = arm-rockchip830-linux-uclibcgnueabihf-gcc

# RV1103 optimization flags - fixed architecture conflict
CFLAGS = -O3 -ffast-math -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -ftree-vectorize

# Strip flags
STRIP = arm-rockchip830-linux-uclibcgnueabihf-strip
STRIP_FLAGS = --strip-all

# Target
TARGET = fb_st7789

# Source
SRC = fb_st7789.c

# Default target
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)
	$(STRIP) $(STRIP_FLAGS) $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
