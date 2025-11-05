#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/spi/spidev.h>
#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/time.h>

// GPIO pins
#define DC_PIN 57
#define RESET_PIN 56

// Display dimensions
#define WIDTH 320
#define HEIGHT 170
#define OFFSET_X 0
#define OFFSET_Y 35

// Quiet mode
#define QUIET_MODE 1

// SPI settings
#define SPI_DEVICE "/dev/spidev0.0"
#define SPI_SPEED 80000000
#define SPI_BITS 8
#define SPI_MODE 0

// Framebuffer
#define FB_DEVICE "/dev/fb0"
#define LINE_LENGTH 640
#define BYTES_PER_PIXEL 2

// Performance tuning
#define CHUNK_LINES 4
#define SPI_CHUNK_SIZE 4096

// Color format - only BGR kept
#define COLOR_FORMAT_BGR 0

// Global variables
static int spi_fd = -1;
static int fb_fd = -1;
static unsigned char *fb_data = NULL;
static size_t fb_size = 0;
static int quit_flag = 0;

// Pre-allocated buffers
static unsigned char *spi_buffer = NULL;
static size_t spi_buffer_size = 0;

// Function prototypes
void print_quiet(const char *format, ...);
int setup_gpio(void);
int gpio_write(int pin, int value);
int init_spi(void);
int init_framebuffer(void);
void write_command(unsigned char cmd);
void write_data_bulk(unsigned char *data, size_t len);
void reset_display(void);
int init_display(void);
void set_window(unsigned short x0, unsigned short y0, unsigned short x1, unsigned short y1);
int render_framebuffer_ultrafast(void);
void fill_color(unsigned char r, unsigned char g, unsigned char b);
void cleanup(void);
void signal_handler(int sig);

// Color conversion function for BGR format
static inline unsigned short convert_pixel_bgr(unsigned char low, unsigned char high) {
    // BGR565: BBBBBGGG GGGRRRRR
    unsigned char b = (high >> 3) & 0x1F;
    unsigned char g = ((high & 0x07) << 3) | ((low >> 5) & 0x07);
    unsigned char r = low & 0x1F;
    return (r << 11) | (g << 5) | b;
}

static inline void write_data_optimized(unsigned char *data, size_t len) {
    gpio_write(DC_PIN, 1);
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)data,
        .rx_buf = 0,
        .len = len,
        .delay_usecs = 0,
        .speed_hz = SPI_SPEED,
        .bits_per_word = SPI_BITS,
    };
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
}

void print_quiet(const char *format, ...) {
    if (!QUIET_MODE) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
}

int setup_gpio(void) {
    char path[64];
    char buffer[16];
    int pins[] = {DC_PIN, RESET_PIN};
    int num_pins = sizeof(pins) / sizeof(pins[0]);
    
    for (int i = 0; i < num_pins; i++) {
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", pins[i]);
        if (access(path, F_OK) != 0) {
            int export_fd = open("/sys/class/gpio/export", O_WRONLY);
            if (export_fd < 0) continue;
            snprintf(buffer, sizeof(buffer), "%d", pins[i]);
            write(export_fd, buffer, strlen(buffer));
            close(export_fd);
            usleep(10000);
        }
    }
    
    usleep(100000);
    
    for (int i = 0; i < num_pins; i++) {
        snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pins[i]);
        int dir_fd = open(path, O_WRONLY);
        if (dir_fd < 0) continue;
        write(dir_fd, "out", 3);
        close(dir_fd);
    }
    
    return 0;
}

int gpio_write(int pin, int value) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int value_fd = open(path, O_WRONLY);
    if (value_fd < 0) return -1;
    write(value_fd, value ? "1" : "0", 1);
    close(value_fd);
    return 0;
}

int init_spi(void) {
    spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) {
        print_quiet("Error: Could not open SPI device\n");
        return -1;
    }
    
    int mode = SPI_MODE;
    int bits = SPI_BITS;
    int speed = SPI_SPEED;
    
    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        print_quiet("Error: Could not set SPI parameters\n");
        close(spi_fd);
        spi_fd = -1;
        return -1;
    }
    
    spi_buffer_size = WIDTH * CHUNK_LINES * 2;
    spi_buffer = malloc(spi_buffer_size + 63);
    if (!spi_buffer) {
        print_quiet("Error: Could not allocate SPI buffer\n");
        close(spi_fd);
        spi_fd = -1;
        return -1;
    }
    
    unsigned char *aligned_buffer = spi_buffer;
    if (((unsigned long)aligned_buffer & 63) != 0) {
        aligned_buffer = (unsigned char*)(((unsigned long)aligned_buffer + 63) & ~63);
    }
    spi_buffer = aligned_buffer;
    
    print_quiet("SPI initialized at %d Hz\n", SPI_SPEED);
    return 0;
}

int init_framebuffer(void) {
    fb_fd = open(FB_DEVICE, O_RDONLY);
    if (fb_fd < 0) {
        print_quiet("Error: Could not open framebuffer device\n");
        return -1;
    }
    
    fb_size = LINE_LENGTH * HEIGHT;
    fb_data = mmap(NULL, fb_size, PROT_READ, MAP_SHARED, fb_fd, 0);
    if (fb_data == MAP_FAILED) {
        print_quiet("Error: Could not mmap framebuffer\n");
        close(fb_fd);
        fb_fd = -1;
        return -1;
    }
    
    print_quiet("Framebuffer initialized\n");
    return 0;
}

void write_command(unsigned char cmd) {
    gpio_write(DC_PIN, 0);
    unsigned char tx_buf[1] = {cmd};
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx_buf,
        .rx_buf = 0,
        .len = 1,
        .delay_usecs = 0,
        .speed_hz = SPI_SPEED,
        .bits_per_word = SPI_BITS,
    };
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
}

void write_data_bulk(unsigned char *data, size_t len) {
    gpio_write(DC_PIN, 1);
    size_t chunk_size = SPI_CHUNK_SIZE;
    for (size_t i = 0; i < len; i += chunk_size) {
        size_t remaining = len - i;
        size_t current_chunk = (remaining < chunk_size) ? remaining : chunk_size;
        struct spi_ioc_transfer tr = {
            .tx_buf = (unsigned long)(data + i),
            .rx_buf = 0,
            .len = current_chunk,
            .delay_usecs = 0,
            .speed_hz = SPI_SPEED,
            .bits_per_word = SPI_BITS,
        };
        ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
    }
}

void reset_display(void) {
    print_quiet("Resetting display...\n");
    gpio_write(RESET_PIN, 1);
    usleep(100000);
    gpio_write(RESET_PIN, 0);
    usleep(100000);
    gpio_write(RESET_PIN, 1);
    usleep(120000);
}

int init_display(void) {
    print_quiet("Initializing display...\n");
    setup_gpio();
    
    if (init_spi() < 0) return -1;
    
    reset_display();
    
    unsigned char init_sequence[] = {
        0x01, 0,                         0x11, 0,
        0x3A, 1, 0x55,                   0x36, 1, 0x60,
        0x2A, 4, 0x00, 0x00, 0x01, 0x3F, 0x2B, 4, 0x00, 0x00, 0x00, 0xEF,
        0x21, 0,                         0x13, 0,
        0x29, 0,
    };
    
    int delay_times[] = {150, 150, 10, 10, 10, 10, 10, 10, 150};
    int seq_index = 0;
    
    for (int i = 0; i < sizeof(delay_times) / sizeof(delay_times[0]); i++) {
        unsigned char cmd = init_sequence[seq_index++];
        unsigned char data_len = init_sequence[seq_index++];
        write_command(cmd);
        if (data_len > 0) {
            write_data_bulk(&init_sequence[seq_index], data_len);
            seq_index += data_len;
        }
        usleep(delay_times[i] * 1000);
    }
    
    print_quiet("Display initialized\n");
    return 0;
}

void set_window(unsigned short x0, unsigned short y0, unsigned short x1, unsigned short y1) {
    unsigned short y0_offset = y0 + OFFSET_Y;
    unsigned short y1_offset = y1 + OFFSET_Y;
    unsigned char window_data[8] = {
        (x0 >> 8) & 0xFF, x0 & 0xFF, (x1 >> 8) & 0xFF, x1 & 0xFF,
        (y0_offset >> 8) & 0xFF, y0_offset & 0xFF, (y1_offset >> 8) & 0xFF, y1_offset & 0xFF
    };
    write_command(0x2A);
    write_data_bulk(window_data, 4);
    write_command(0x2B);
    write_data_bulk(window_data + 4, 4);
    write_command(0x2C);
}

int render_framebuffer_ultrafast(void) {
    if (!fb_data || !spi_buffer) return -1;
    
    set_window(0, 0, WIDTH - 1, HEIGHT - 1);
    
    for (int chunk_start = 0; chunk_start < HEIGHT; chunk_start += CHUNK_LINES) {
        int chunk_end = chunk_start + CHUNK_LINES;
        if (chunk_end > HEIGHT) chunk_end = HEIGHT;
        
        int chunk_index = 0;
        
        for (int y = chunk_start; y < chunk_end; y++) {
            size_t line_start = y * LINE_LENGTH;
            
            for (int x = 0; x < WIDTH; x++) {
                size_t pixel_start = line_start + (x * BYTES_PER_PIXEL);
                unsigned char pixel_low = fb_data[pixel_start];
                unsigned char pixel_high = fb_data[pixel_start + 1];
                
                unsigned short display_color = convert_pixel_bgr(pixel_low, pixel_high);
                
                spi_buffer[chunk_index++] = (display_color >> 8) & 0xFF;
                spi_buffer[chunk_index++] = display_color & 0xFF;
            }
        }
        
        write_data_optimized(spi_buffer, chunk_index);
    }
    
    return 0;
}

void fill_color(unsigned char r, unsigned char g, unsigned char b) {
    // Always use correct RGB order for fill colors (for testing)
    unsigned short color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    unsigned char color_high = color >> 8;
    unsigned char color_low = color & 0xFF;
    
    print_quiet("Testing RGB(%d,%d,%d)\n", r, g, b);
    
    set_window(0, 0, WIDTH - 1, HEIGHT - 1);
    
    int pixels_per_fill = spi_buffer_size / 2;
    for (int i = 0; i < pixels_per_fill * 2; i += 2) {
        spi_buffer[i] = color_high;
        spi_buffer[i + 1] = color_low;
    }
    
    int total_pixels = WIDTH * HEIGHT;
    int sent_pixels = 0;
    
    while (sent_pixels < total_pixels) {
        int chunk_pixels = (total_pixels - sent_pixels < pixels_per_fill) ? 
                          (total_pixels - sent_pixels) : pixels_per_fill;
        write_data_optimized(spi_buffer, chunk_pixels * 2);
        sent_pixels += chunk_pixels;
    }
}

void cleanup(void) {
    if (spi_buffer) free(spi_buffer);
    if (fb_data && fb_data != MAP_FAILED) munmap(fb_data, fb_size);
    if (fb_fd >= 0) close(fb_fd);
    if (spi_fd >= 0) close(spi_fd);
}

void signal_handler(int sig) {
    quit_flag = 1;
}

double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

int main(int argc, char *argv[]) {
    int test_mode = 0;
    int quiet_mode = QUIET_MODE;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0 || strcmp(argv[i], "-t") == 0) {
            test_mode = 1;
        } else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            quiet_mode = 1;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    print_quiet("Starting ST7789 renderer with BGR color format...\n");
    
    if (init_display() < 0) {
        cleanup();
        return 1;
    }
    
    if (init_framebuffer() < 0) {
        cleanup();
        return 1;
    }
    
    fill_color(0, 0, 0);
    usleep(50000);
    
    if (test_mode) {
        print_quiet("Quick test with BGR format...\n");
        fill_color(255, 0, 0); usleep(500000);
        fill_color(0, 255, 0); usleep(500000);
        fill_color(0, 0, 255); usleep(500000);
        fill_color(0, 0, 0); usleep(100000);
    }
    
    int frame_count = 0;
    double start_time = get_time_ms();
    double fps_time = start_time;
    
    while (!quit_flag) {
        frame_count++;
        render_framebuffer_ultrafast();
        
        if (!quiet_mode && frame_count % 120 == 0) {
            double current_time = get_time_ms();
            double elapsed = current_time - fps_time;
            double fps = 120.0 / (elapsed / 1000.0);
            print_quiet("Frame %d: %.1f FPS\n", frame_count, fps);
            fps_time = current_time;
        }
    }
    
    print_quiet("Rendered %d frames\n", frame_count);
    cleanup();
    return 0;
}
