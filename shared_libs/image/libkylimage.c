#define _POSIX_C_SOURCE 200809L  // for strdup
#include "libkylimage.h"
#include "../kuyil_types.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // for strcasecmp
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Image loading using stb_image
KylImage* image_load(const char* filepath) {
    if (!filepath) return NULL;
    
    int width, height, channels;
    unsigned char* data = stbi_load(filepath, &width, &height, &channels, 0);
    
    if (!data) {
        fprintf(stderr, "Failed to load image: %s\n", filepath);
        return NULL;
    }
    
    KylImage* img = (KylImage*)malloc(sizeof(KylImage));
    if (!img) {
        stbi_image_free(data);
        return NULL;
    }
    
    img->width = width;
    img->height = height;
    img->channels = channels;
    img->data = data;  // Use stbi_image_free in image_destroy
    
    return img;
}

// Image saving using stb_image_write
bool image_save(KylImage* img, const char* filepath, const char* format) {
    if (!img || !filepath || !img->data) return false;
    
    int result = 0;
    const char* ext = strrchr(filepath, '.');
    
    if (!ext) {
        // No extension, use format parameter
        ext = format;
    } else {
        ext++; // Skip the dot
    }
    
    if (!ext) {
        fprintf(stderr, "No file format specified\n");
        return false;
    }
    
    // Determine format and save
    if (strcasecmp(ext, "png") == 0) {
        result = stbi_write_png(filepath, img->width, img->height, img->channels, 
                                img->data, img->width * img->channels);
    } else if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) {
        result = stbi_write_jpg(filepath, img->width, img->height, img->channels, 
                                img->data, 90);  // Quality: 90
    } else if (strcasecmp(ext, "bmp") == 0) {
        result = stbi_write_bmp(filepath, img->width, img->height, img->channels, 
                                img->data);
    } else if (strcasecmp(ext, "tga") == 0) {
        result = stbi_write_tga(filepath, img->width, img->height, img->channels, 
                                img->data);
    } else {
        fprintf(stderr, "Unsupported format: %s\n", ext);
        return false;
    }
    
    if (!result) {
        fprintf(stderr, "Failed to save image: %s\n", filepath);
    }
    
    return result != 0;
}

KylImage* image_create(int32_t width, int32_t height, int32_t channels) {
    KylImage* img = (KylImage*)malloc(sizeof(KylImage));
    if (!img) return NULL;
    
    img->width = width;
    img->height = height;
    img->channels = channels;
    img->data = (uint8_t*)calloc(width * height * channels, sizeof(uint8_t));
    
    if (!img->data) {
        free(img);
        return NULL;
    }
    
    return img;
}

void image_destroy(KylImage* img) {
    if (img) {
        if (img->data) {
            // Use stbi_image_free for images loaded with stbi_load
            // Note: This is safe to use for malloc'd data too (it just calls free)
            stbi_image_free(img->data);
        }
        free(img);
    }
}

// Image information
int32_t image_width(KylImage* img) {
    return img ? img->width : 0;
}

int32_t image_height(KylImage* img) {
    return img ? img->height : 0;
}

int32_t image_channels(KylImage* img) {
    return img ? img->channels : 0;
}

// Image manipulation (stub implementations)
KylImage* image_resize(KylImage* img, int32_t width, int32_t height, const char* interpolation) {
    // TODO: Implement actual resizing
    (void)img;
    (void)width;
    (void)height;
    (void)interpolation;
    return NULL;
}

KylImage* image_crop(KylImage* img, int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!img || !img->data) return NULL;
    if (x < 0 || y < 0 || x + width > img->width || y + height > img->height) return NULL;
    
    KylImage* cropped = image_create(width, height, img->channels);
    if (!cropped) return NULL;
    
    for (int32_t row = 0; row < height; row++) {
        for (int32_t col = 0; col < width; col++) {
            int32_t src_idx = ((y + row) * img->width + (x + col)) * img->channels;
            int32_t dst_idx = (row * width + col) * img->channels;
            memcpy(&cropped->data[dst_idx], &img->data[src_idx], img->channels);
        }
    }
    
    return cropped;
}

KylImage* image_rotate(KylImage* img, double angle) {
    // TODO: Implement actual rotation
    (void)img;
    (void)angle;
    return NULL;
}

KylImage* image_flip(KylImage* img, bool horizontal) {
    // TODO: Implement flip
    (void)img;
    (void)horizontal;
    return NULL;
}

// Pixel operations
void* image_get_pixel(KylImage* img, int32_t x, int32_t y) {
    // TODO: Return pixel data as object
    (void)img;
    (void)x;
    (void)y;
    return NULL;
}

void image_set_pixel(KylImage* img, int32_t x, int32_t y, int32_t r, int32_t g, int32_t b, int32_t a) {
    if (!img || !img->data) return;
    if (x < 0 || x >= img->width || y < 0 || y >= img->height) return;
    
    int32_t idx = (y * img->width + x) * img->channels;
    if (img->channels >= 1) img->data[idx] = (uint8_t)r;
    if (img->channels >= 2) img->data[idx + 1] = (uint8_t)g;
    if (img->channels >= 3) img->data[idx + 2] = (uint8_t)b;
    if (img->channels >= 4) img->data[idx + 3] = (uint8_t)a;
}

// Image filters
KylImage* image_grayscale(KylImage* img) {
    if (!img || img->channels < 3) return NULL;
    
    KylImage* gray = image_create(img->width, img->height, 1);
    if (!gray) return NULL;
    
    for (int32_t i = 0; i < img->width * img->height; i++) {
        int32_t src_idx = i * img->channels;
        uint8_t r = img->data[src_idx];
        uint8_t g = img->data[src_idx + 1];
        uint8_t b = img->data[src_idx + 2];
        gray->data[i] = (uint8_t)(0.299 * r + 0.587 * g + 0.114 * b);
    }
    
    return gray;
}

KylImage* image_blur(KylImage* img, double radius) {
    // TODO: Implement Gaussian blur
    (void)img;
    (void)radius;
    return NULL;
}

KylImage* image_sharpen(KylImage* img, double amount) {
    // TODO: Implement unsharp mask
    (void)img;
    (void)amount;
    return NULL;
}

KylImage* image_brightness(KylImage* img, double factor) {
    if (!img) return NULL;
    
    KylImage* result = image_create(img->width, img->height, img->channels);
    if (!result) return NULL;
    
    for (int32_t i = 0; i < img->width * img->height * img->channels; i++) {
        int32_t val = (int32_t)(img->data[i] * factor);
        result->data[i] = (uint8_t)(val > 255 ? 255 : (val < 0 ? 0 : val));
    }
    
    return result;
}

KylImage* image_contrast(KylImage* img, double factor) {
    // TODO: Implement contrast adjustment
    (void)img;
    (void)factor;
    return NULL;
}

KylImage* image_threshold(KylImage* img, int32_t threshold) {
    // TODO: Implement thresholding
    (void)img;
    (void)threshold;
    return NULL;
}

KylImage* image_edge_detect(KylImage* img, const char* method) {
    // TODO: Implement edge detection (Sobel, Canny, etc.)
    (void)img;
    (void)method;
    return NULL;
}

// Image composition
KylImage* image_blend(KylImage* img1, KylImage* img2, double alpha) {
    // TODO: Implement alpha blending
    (void)img1;
    (void)img2;
    (void)alpha;
    return NULL;
}

KylImage* image_overlay(KylImage* background, KylImage* foreground, int32_t x, int32_t y) {
    // TODO: Implement overlay
    (void)background;
    (void)foreground;
    (void)x;
    (void)y;
    return NULL;
}

// Color space conversion
KylImage* image_rgb_to_hsv(KylImage* img) {
    // TODO: Implement RGB to HSV conversion
    (void)img;
    return NULL;
}

KylImage* image_hsv_to_rgb(KylImage* img) {
    // TODO: Implement HSV to RGB conversion
    (void)img;
    return NULL;
}

// ============================================================================
// Kuyil FFI Wrapper Functions
// These functions are called by Kuyil VM and handle object wrapping/unwrapping
// ============================================================================

Value kyl_image_create(int arg_count, Value* args) {
    if (arg_count < 3 || args[0].type != VALUE_NUMBER || 
        args[1].type != VALUE_NUMBER || args[2].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    int32_t width = (int32_t)args[0].as.number;
    int32_t height = (int32_t)args[1].as.number;
    int32_t channels = (int32_t)args[2].as.number;
    
    KylImage* img = image_create(width, height, channels);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)img;
    return result;
}

Value kyl_image_destroy(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    image_destroy(img);
    
    Value result = {VALUE_NIL};
    return result;
}

Value kyl_image_width(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)image_width(img);
    return result;
}

Value kyl_image_height(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)image_height(img);
    return result;
}

Value kyl_image_channels(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)image_channels(img);
    return result;
}

Value kyl_image_set_pixel(int arg_count, Value* args) {
    if (arg_count < 7 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    int32_t x = (int32_t)args[1].as.number;
    int32_t y = (int32_t)args[2].as.number;
    int32_t r = (int32_t)args[3].as.number;
    int32_t g = (int32_t)args[4].as.number;
    int32_t b = (int32_t)args[5].as.number;
    int32_t a = (int32_t)args[6].as.number;
    
    image_set_pixel(img, x, y, r, g, b, a);
    
    Value result = {VALUE_NIL};
    return result;
}

Value kyl_image_crop(int arg_count, Value* args) {
    if (arg_count < 5 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    int32_t x = (int32_t)args[1].as.number;
    int32_t y = (int32_t)args[2].as.number;
    int32_t width = (int32_t)args[3].as.number;
    int32_t height = (int32_t)args[4].as.number;
    
    KylImage* cropped = image_crop(img, x, y, width, height);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)cropped;
    return result;
}

Value kyl_image_grayscale(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    KylImage* gray = image_grayscale(img);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)gray;
    return result;
}

Value kyl_image_brightness(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    double factor = args[1].as.number;
    
    KylImage* result_img = image_brightness(img, factor);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)(uintptr_t)result_img;
    return result;
}

Value kyl_image_load(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_STRING) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    const char* filepath = args[0].as.string;
    KylImage* img = image_load(filepath);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    if (img) {
        result.type = VALUE_NUMBER;
        result.as.number = (double)(uintptr_t)img;
    } else {
        result.type = VALUE_NIL;
    }
    return result;
}

Value kyl_image_save(int arg_count, Value* args) {
    if (arg_count < 2 || args[0].type != VALUE_NUMBER || args[1].type != VALUE_STRING) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    const char* filepath = args[1].as.string;
    const char* format = NULL;
    
    if (arg_count >= 3 && args[2].type == VALUE_STRING) {
        format = args[2].as.string;
    }
    
    bool success = image_save(img, filepath, format);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_BOOL;
    result.as.boolean = success;
    return result;
}

// ===== HTML5 Canvas-inspired drawing functions =====

// Helper: clamp value between min and max
static inline int32_t clamp(int32_t val, int32_t min, int32_t max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

// fillRect(x, y, width, height) - fill rectangle with current color
void canvas_fillRect(KylImage* img, int32_t x, int32_t y, int32_t width, int32_t height,
                     uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!img || !img->data) return;
    
    for (int32_t dy = 0; dy < height; dy++) {
        for (int32_t dx = 0; dx < width; dx++) {
            int32_t px = x + dx;
            int32_t py = y + dy;
            if (px >= 0 && px < img->width && py >= 0 && py < img->height) {
                image_set_pixel(img, px, py, r, g, b, a);
            }
        }
    }
}

// strokeRect(x, y, width, height) - draw rectangle outline
void canvas_strokeRect(KylImage* img, int32_t x, int32_t y, int32_t width, int32_t height,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!img || !img->data) return;
    
    // Top and bottom edges
    for (int32_t dx = 0; dx < width; dx++) {
        int32_t px = x + dx;
        if (px >= 0 && px < img->width) {
            if (y >= 0 && y < img->height) {
                image_set_pixel(img, px, y, r, g, b, a);
            }
            int32_t bottom = y + height - 1;
            if (bottom >= 0 && bottom < img->height) {
                image_set_pixel(img, px, bottom, r, g, b, a);
            }
        }
    }
    
    // Left and right edges
    for (int32_t dy = 0; dy < height; dy++) {
        int32_t py = y + dy;
        if (py >= 0 && py < img->height) {
            if (x >= 0 && x < img->width) {
                image_set_pixel(img, x, py, r, g, b, a);
            }
            int32_t right = x + width - 1;
            if (right >= 0 && right < img->width) {
                image_set_pixel(img, right, py, r, g, b, a);
            }
        }
    }
}

// clearRect(x, y, width, height) - clear rectangle to transparent/white
void canvas_clearRect(KylImage* img, int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!img || !img->data) return;
    canvas_fillRect(img, x, y, width, height, 255, 255, 255, 255);
}

// fillCircle(x, y, radius) - fill circle
void canvas_fillCircle(KylImage* img, int32_t cx, int32_t cy, int32_t radius,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!img || !img->data) return;
    
    int32_t r_sq = radius * radius;
    for (int32_t dy = -radius; dy <= radius; dy++) {
        for (int32_t dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= r_sq) {
                int32_t px = cx + dx;
                int32_t py = cy + dy;
                if (px >= 0 && px < img->width && py >= 0 && py < img->height) {
                    image_set_pixel(img, px, py, r, g, b, a);
                }
            }
        }
    }
}

// strokeCircle(x, y, radius) - draw circle outline
void canvas_strokeCircle(KylImage* img, int32_t cx, int32_t cy, int32_t radius,
                         uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!img || !img->data) return;
    
    // Midpoint circle algorithm
    int32_t x = radius;
    int32_t y = 0;
    int32_t err = 0;
    
    while (x >= y) {
        // Draw 8 symmetric points
        if (cx + x >= 0 && cx + x < img->width && cy + y >= 0 && cy + y < img->height)
            image_set_pixel(img, cx + x, cy + y, r, g, b, a);
        if (cx + y >= 0 && cx + y < img->width && cy + x >= 0 && cy + x < img->height)
            image_set_pixel(img, cx + y, cy + x, r, g, b, a);
        if (cx - y >= 0 && cx - y < img->width && cy + x >= 0 && cy + x < img->height)
            image_set_pixel(img, cx - y, cy + x, r, g, b, a);
        if (cx - x >= 0 && cx - x < img->width && cy + y >= 0 && cy + y < img->height)
            image_set_pixel(img, cx - x, cy + y, r, g, b, a);
        if (cx - x >= 0 && cx - x < img->width && cy - y >= 0 && cy - y < img->height)
            image_set_pixel(img, cx - x, cy - y, r, g, b, a);
        if (cx - y >= 0 && cx - y < img->width && cy - x >= 0 && cy - x < img->height)
            image_set_pixel(img, cx - y, cy - x, r, g, b, a);
        if (cx + y >= 0 && cx + y < img->width && cy - x >= 0 && cy - x < img->height)
            image_set_pixel(img, cx + y, cy - x, r, g, b, a);
        if (cx + x >= 0 && cx + x < img->width && cy - y >= 0 && cy - y < img->height)
            image_set_pixel(img, cx + x, cy - y, r, g, b, a);
        
        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) {
            x--;
            err += 1 - 2 * x;
        }
    }
}

// drawLine(x0, y0, x1, y1) - draw line using Bresenham
void canvas_drawLine(KylImage* img, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                     uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!img || !img->data) return;
    
    int32_t dx = abs(x1 - x0);
    int32_t dy = abs(y1 - y0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx - dy;
    
    int32_t x = x0;
    int32_t y = y0;
    
    while (1) {
        if (x >= 0 && x < img->width && y >= 0 && y < img->height) {
            image_set_pixel(img, x, y, r, g, b, a);
        }
        
        if (x == x1 && y == y1) break;
        
        int32_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

// Wrapper functions for Kuyil FFI
Value kyl_canvas_fillRect(int arg_count, Value* args) {
    if (arg_count < 8) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    int32_t x = (int32_t)args[1].as.number;
    int32_t y = (int32_t)args[2].as.number;
    int32_t w = (int32_t)args[3].as.number;
    int32_t h = (int32_t)args[4].as.number;
    uint8_t r = (uint8_t)args[5].as.number;
    uint8_t g = (uint8_t)args[6].as.number;
    uint8_t b = (uint8_t)args[7].as.number;
    uint8_t a = arg_count >= 9 ? (uint8_t)args[8].as.number : 255;
    
    canvas_fillRect(img, x, y, w, h, r, g, b, a);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_canvas_strokeRect(int arg_count, Value* args) {
    if (arg_count < 8) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    int32_t x = (int32_t)args[1].as.number;
    int32_t y = (int32_t)args[2].as.number;
    int32_t w = (int32_t)args[3].as.number;
    int32_t h = (int32_t)args[4].as.number;
    uint8_t r = (uint8_t)args[5].as.number;
    uint8_t g = (uint8_t)args[6].as.number;
    uint8_t b = (uint8_t)args[7].as.number;
    uint8_t a = arg_count >= 9 ? (uint8_t)args[8].as.number : 255;
    
    canvas_strokeRect(img, x, y, w, h, r, g, b, a);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_canvas_clearRect(int arg_count, Value* args) {
    if (arg_count < 5) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    int32_t x = (int32_t)args[1].as.number;
    int32_t y = (int32_t)args[2].as.number;
    int32_t w = (int32_t)args[3].as.number;
    int32_t h = (int32_t)args[4].as.number;
    
    canvas_clearRect(img, x, y, w, h);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_canvas_fillCircle(int arg_count, Value* args) {
    if (arg_count < 7) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    int32_t cx = (int32_t)args[1].as.number;
    int32_t cy = (int32_t)args[2].as.number;
    int32_t radius = (int32_t)args[3].as.number;
    uint8_t r = (uint8_t)args[4].as.number;
    uint8_t g = (uint8_t)args[5].as.number;
    uint8_t b = (uint8_t)args[6].as.number;
    uint8_t a = arg_count >= 8 ? (uint8_t)args[7].as.number : 255;
    
    canvas_fillCircle(img, cx, cy, radius, r, g, b, a);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_canvas_strokeCircle(int arg_count, Value* args) {
    if (arg_count < 7) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    int32_t cx = (int32_t)args[1].as.number;
    int32_t cy = (int32_t)args[2].as.number;
    int32_t radius = (int32_t)args[3].as.number;
    uint8_t r = (uint8_t)args[4].as.number;
    uint8_t g = (uint8_t)args[5].as.number;
    uint8_t b = (uint8_t)args[6].as.number;
    uint8_t a = arg_count >= 8 ? (uint8_t)args[7].as.number : 255;
    
    canvas_strokeCircle(img, cx, cy, radius, r, g, b, a);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_canvas_drawLine(int arg_count, Value* args) {
    if (arg_count < 8) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    int32_t x0 = (int32_t)args[1].as.number;
    int32_t y0 = (int32_t)args[2].as.number;
    int32_t x1 = (int32_t)args[3].as.number;
    int32_t y1 = (int32_t)args[4].as.number;
    uint8_t r = (uint8_t)args[5].as.number;
    uint8_t g = (uint8_t)args[6].as.number;
    uint8_t b = (uint8_t)args[7].as.number;
    uint8_t a = arg_count >= 9 ? (uint8_t)args[8].as.number : 255;
    
    canvas_drawLine(img, x0, y0, x1, y1, r, g, b, a);
    
    Value nil = {VALUE_NIL};
    return nil;
}

// ===== Path2D and curve drawing =====

typedef struct {
    double* points_x;
    double* points_y;
    int32_t count;
    int32_t capacity;
    double current_x;
    double current_y;
} Path2D;

Path2D* path2d_create() {
    Path2D* path = (Path2D*)malloc(sizeof(Path2D));
    if (!path) return NULL;
    
    path->capacity = 256;
    path->points_x = (double*)malloc(path->capacity * sizeof(double));
    path->points_y = (double*)malloc(path->capacity * sizeof(double));
    path->count = 0;
    path->current_x = 0;
    path->current_y = 0;
    
    if (!path->points_x || !path->points_y) {
        free(path->points_x);
        free(path->points_y);
        free(path);
        return NULL;
    }
    
    return path;
}

void path2d_destroy(Path2D* path) {
    if (!path) return;
    free(path->points_x);
    free(path->points_y);
    free(path);
}

void path2d_moveTo(Path2D* path, double x, double y) {
    if (!path) return;
    path->current_x = x;
    path->current_y = y;
}

void path2d_lineTo(Path2D* path, double x, double y) {
    if (!path) return;
    
    if (path->count + 2 >= path->capacity) {
        path->capacity *= 2;
        path->points_x = (double*)realloc(path->points_x, path->capacity * sizeof(double));
        path->points_y = (double*)realloc(path->points_y, path->capacity * sizeof(double));
    }
    
    path->points_x[path->count] = path->current_x;
    path->points_y[path->count] = path->current_y;
    path->count++;
    
    path->points_x[path->count] = x;
    path->points_y[path->count] = y;
    path->count++;
    
    path->current_x = x;
    path->current_y = y;
}

void path2d_quadraticCurveTo(Path2D* path, double cpx, double cpy, double x, double y) {
    if (!path) return;
    
    // Approximate quadratic Bezier with line segments
    int steps = 20;
    double x0 = path->current_x;
    double y0 = path->current_y;
    
    for (int i = 1; i <= steps; i++) {
        double t = (double)i / steps;
        double t_inv = 1.0 - t;
        double bx = t_inv * t_inv * x0 + 2.0 * t_inv * t * cpx + t * t * x;
        double by = t_inv * t_inv * y0 + 2.0 * t_inv * t * cpy + t * t * y;
        path2d_lineTo(path, bx, by);
    }
    
    path->current_x = x;
    path->current_y = y;
}

void path2d_bezierCurveTo(Path2D* path, double cp1x, double cp1y, double cp2x, double cp2y, double x, double y) {
    if (!path) return;
    
    // Approximate cubic Bezier with line segments
    int steps = 30;
    double x0 = path->current_x;
    double y0 = path->current_y;
    
    for (int i = 1; i <= steps; i++) {
        double t = (double)i / steps;
        double t_inv = 1.0 - t;
        double t_inv2 = t_inv * t_inv;
        double t_inv3 = t_inv2 * t_inv;
        double t2 = t * t;
        double t3 = t2 * t;
        
        double bx = t_inv3 * x0 + 3.0 * t_inv2 * t * cp1x + 3.0 * t_inv * t2 * cp2x + t3 * x;
        double by = t_inv3 * y0 + 3.0 * t_inv2 * t * cp1y + 3.0 * t_inv * t2 * cp2y + t3 * y;
        path2d_lineTo(path, bx, by);
    }
    
    path->current_x = x;
    path->current_y = y;
}

void path2d_stroke(KylImage* img, Path2D* path, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!img || !path) return;
    
    for (int i = 0; i < path->count; i += 2) {
        if (i + 1 < path->count) {
            int32_t x0 = (int32_t)path->points_x[i];
            int32_t y0 = (int32_t)path->points_y[i];
            int32_t x1 = (int32_t)path->points_x[i + 1];
            int32_t y1 = (int32_t)path->points_y[i + 1];
            canvas_drawLine(img, x0, y0, x1, y1, r, g, b, a);
        }
    }
}

// ===== Text rendering (proper bitmap font) =====

// 8x8 bitmap font - higher resolution for sharper text
// Each character defined as 8 bytes (8 rows of 8 bits each)
static const uint8_t font8x8[][8] = {
    // A-Z (uppercase) - cleaner, sharper designs
    {0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00}, // A
    {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00}, // B
    {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00}, // C
    {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00}, // D
    {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00}, // E
    {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00}, // F
    {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3E, 0x00}, // G
    {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00}, // H
    {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, // I
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00}, // J
    {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00}, // K
    {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00}, // L
    {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00}, // M
    {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00}, // N
    {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // O
    {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00}, // P
    {0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x0E, 0x00}, // Q
    {0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00}, // R
    {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00}, // S
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00}, // T
    {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // U
    {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00}, // V
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // W
    {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00}, // X
    {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00}, // Y
    {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00}, // Z
};

void canvas_drawChar(KylImage* img, char ch, int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!img) return;
    
    int char_index = -1;
    
    // Map character to font index
    if (ch >= 'A' && ch <= 'Z') {
        char_index = ch - 'A';
    } else if (ch >= 'a' && ch <= 'z') {
        char_index = ch - 'a'; // Use same glyphs for lowercase
    } else if (ch == ' ') {
        return; // Space - just skip
    } else {
        return; // Unsupported character
    }
    
    // Draw the character from bitmap (8x8)
    for (int row = 0; row < 8; row++) {
        uint8_t bits = font8x8[char_index][row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) { // Check bit from left to right
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < img->width && py >= 0 && py < img->height) {
                    image_set_pixel(img, px, py, r, g, b, a);
                }
            }
        }
    }
}

// Draw character with scaling (1x, 2x, 3x, etc.)
void canvas_drawCharScaled(KylImage* img, char ch, int32_t x, int32_t y, 
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a, int32_t scale) {
    if (!img || scale < 1) return;
    
    int char_index = -1;
    
    // Map character to font index
    if (ch >= 'A' && ch <= 'Z') {
        char_index = ch - 'A';
    } else if (ch >= 'a' && ch <= 'z') {
        char_index = ch - 'a';
    } else if (ch == ' ') {
        return; // Space - just skip
    } else {
        return; // Unsupported character
    }
    
    // Draw the character with scaling
    for (int row = 0; row < 8; row++) {
        uint8_t bits = font8x8[char_index][row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                // Draw a scale x scale block for each pixel
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        if (px >= 0 && px < img->width && py >= 0 && py < img->height) {
                            image_set_pixel(img, px, py, r, g, b, a);
                        }
                    }
                }
            }
        }
    }
}

void canvas_fillText(KylImage* img, const char* text, int32_t x, int32_t y, 
                     uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!img || !text) return;
    
    int offset = 0;
    for (int i = 0; text[i] != '\0'; i++) {
        canvas_drawChar(img, text[i], x + offset, y, r, g, b, a);
        offset += 9; // 8 pixels + 1 spacing
    }
}

void canvas_strokeText(KylImage* img, const char* text, int32_t x, int32_t y,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    // For simple implementation, strokeText is same as fillText
    canvas_fillText(img, text, x, y, r, g, b, a);
}

// Draw text with custom scale (1x, 2x, 3x, etc.)
void canvas_fillTextScaled(KylImage* img, const char* text, int32_t x, int32_t y, 
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a, int32_t scale) {
    if (!img || !text || scale < 1) return;
    
    int offset = 0;
    for (int i = 0; text[i] != '\0'; i++) {
        canvas_drawCharScaled(img, text[i], x + offset, y, r, g, b, a, scale);
        offset += (8 * scale) + scale; // scaled character width + scaled spacing
    }
}

void canvas_strokeTextScaled(KylImage* img, const char* text, int32_t x, int32_t y,
                            uint8_t r, uint8_t g, uint8_t b, uint8_t a, int32_t scale) {
    // For simple implementation, strokeText is same as fillText
    canvas_fillTextScaled(img, text, x, y, r, g, b, a, scale);
}

// Calculate text width in pixels (scale 1x = 9 pixels per char)
int32_t canvas_measureText(const char* text, int32_t scale) {
    if (!text || scale < 1) return 0;
    
    int length = 0;
    for (int i = 0; text[i] != '\0'; i++) {
        length++;
    }
    
    if (length == 0) return 0;
    
    // Each character is 8 pixels wide + 1 pixel spacing, multiplied by scale
    // Last character doesn't need spacing
    return (length * 8 * scale) + ((length - 1) * scale);
}

// Calculate text height in pixels (always 8 * scale)
int32_t canvas_textHeight(int32_t scale) {
    if (scale < 1) return 0;
    return 8 * scale;
}

// ===== Transform functions =====

typedef struct {
    double m[6]; // [a, b, c, d, e, f] for transform matrix
} Transform2D;

Transform2D* transform_create() {
    Transform2D* t = (Transform2D*)malloc(sizeof(Transform2D));
    if (!t) return NULL;
    
    // Identity matrix
    t->m[0] = 1.0; t->m[1] = 0.0;
    t->m[2] = 0.0; t->m[3] = 1.0;
    t->m[4] = 0.0; t->m[5] = 0.0;
    
    return t;
}

void transform_destroy(Transform2D* t) {
    free(t);
}

void transform_translate(Transform2D* t, double x, double y) {
    if (!t) return;
    t->m[4] += t->m[0] * x + t->m[2] * y;
    t->m[5] += t->m[1] * x + t->m[3] * y;
}

void transform_rotate(Transform2D* t, double angle) {
    if (!t) return;
    
    double c = cos(angle);
    double s = sin(angle);
    
    double m0 = t->m[0];
    double m1 = t->m[1];
    double m2 = t->m[2];
    double m3 = t->m[3];
    
    t->m[0] = m0 * c + m2 * s;
    t->m[1] = m1 * c + m3 * s;
    t->m[2] = m0 * -s + m2 * c;
    t->m[3] = m1 * -s + m3 * c;
}

void transform_scale(Transform2D* t, double sx, double sy) {
    if (!t) return;
    t->m[0] *= sx;
    t->m[1] *= sx;
    t->m[2] *= sy;
    t->m[3] *= sy;
}

void transform_point(Transform2D* t, double* x, double* y) {
    if (!t || !x || !y) return;
    
    double tx = t->m[0] * (*x) + t->m[2] * (*y) + t->m[4];
    double ty = t->m[1] * (*x) + t->m[3] * (*y) + t->m[5];
    
    *x = tx;
    *y = ty;
}

// Bilinear interpolation helper
static inline uint8_t bilinear_sample(KylImage* img, double x, double y, int channel) {
    if (x < 0 || x >= img->width - 1 || y < 0 || y >= img->height - 1) {
        return 255; // White/transparent
    }
    
    int x0 = (int)x;
    int y0 = (int)y;
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    
    double fx = x - x0;
    double fy = y - y0;
    
    int idx00 = (y0 * img->width + x0) * img->channels + channel;
    int idx01 = (y0 * img->width + x1) * img->channels + channel;
    int idx10 = (y1 * img->width + x0) * img->channels + channel;
    int idx11 = (y1 * img->width + x1) * img->channels + channel;
    
    double v00 = img->data[idx00];
    double v01 = img->data[idx01];
    double v10 = img->data[idx10];
    double v11 = img->data[idx11];
    
    double v0 = v00 * (1.0 - fx) + v01 * fx;
    double v1 = v10 * (1.0 - fx) + v11 * fx;
    double v = v0 * (1.0 - fy) + v1 * fy;
    
    return (uint8_t)(v + 0.5);
}

// Apply transform to image with bilinear interpolation (rotate/scale/translate)
KylImage* canvas_applyTransform(KylImage* src, Transform2D* t) {
    if (!src || !t) return NULL;
    
    // Create new image with same dimensions
    KylImage* dst = image_create(src->width, src->height, src->channels);
    if (!dst) return NULL;
    
    // Fill with white/transparent
    memset(dst->data, 255, src->width * src->height * src->channels);
    
    // Compute inverse transform matrix once
    double det = t->m[0] * t->m[3] - t->m[1] * t->m[2];
    if (fabs(det) < 0.0001) {
        return dst; // Singular matrix, return white image
    }
    
    double inv_det = 1.0 / det;
    double inv_m[6];
    inv_m[0] = inv_det * t->m[3];
    inv_m[1] = inv_det * (-t->m[1]);
    inv_m[2] = inv_det * (-t->m[2]);
    inv_m[3] = inv_det * t->m[0];
    inv_m[4] = inv_det * (t->m[2] * t->m[5] - t->m[3] * t->m[4]);
    inv_m[5] = inv_det * (t->m[1] * t->m[4] - t->m[0] * t->m[5]);
    
    // Apply inverse transform with bilinear interpolation
    for (int32_t y = 0; y < dst->height; y++) {
        for (int32_t x = 0; x < dst->width; x++) {
            // Map destination pixel to source coordinates
            double sx = inv_m[0] * x + inv_m[2] * y + inv_m[4];
            double sy = inv_m[1] * x + inv_m[3] * y + inv_m[5];
            
            // Bilinear interpolation for smooth results
            if (sx >= 0 && sx < src->width - 1 && sy >= 0 && sy < src->height - 1) {
                int dst_idx = (y * dst->width + x) * dst->channels;
                
                for (int c = 0; c < src->channels; c++) {
                    dst->data[dst_idx + c] = bilinear_sample(src, sx, sy, c);
                }
            }
        }
    }
    
    return dst;
}

// Draw source image onto destination image at position (dx, dy)
void canvas_drawImage(KylImage* dst, KylImage* src, int32_t dx, int32_t dy) {
    if (!dst || !src) return;
    if (dst->channels != src->channels) return;
    
    for (int32_t y = 0; y < src->height; y++) {
        for (int32_t x = 0; x < src->width; x++) {
            int32_t dst_x = dx + x;
            int32_t dst_y = dy + y;
            
            // Skip if outside destination bounds
            if (dst_x < 0 || dst_x >= dst->width || dst_y < 0 || dst_y >= dst->height) {
                continue;
            }
            
            int src_idx = (y * src->width + x) * src->channels;
            int dst_idx = (dst_y * dst->width + dst_x) * dst->channels;
            
            // Copy pixel (simple overwrite for now)
            for (int c = 0; c < src->channels; c++) {
                dst->data[dst_idx + c] = src->data[src_idx + c];
            }
        }
    }
}

// Draw source image onto destination with scaling
void canvas_drawImageScaled(KylImage* dst, KylImage* src, int32_t dx, int32_t dy, 
                           int32_t dwidth, int32_t dheight) {
    if (!dst || !src) return;
    if (dst->channels != src->channels) return;
    if (dwidth <= 0 || dheight <= 0) return;
    
    double scale_x = (double)src->width / dwidth;
    double scale_y = (double)src->height / dheight;
    
    for (int32_t y = 0; y < dheight; y++) {
        for (int32_t x = 0; x < dwidth; x++) {
            int32_t dst_x = dx + x;
            int32_t dst_y = dy + y;
            
            // Skip if outside destination bounds
            if (dst_x < 0 || dst_x >= dst->width || dst_y < 0 || dst_y >= dst->height) {
                continue;
            }
            
            // Map to source coordinates with bilinear sampling
            double sx = x * scale_x;
            double sy = y * scale_y;
            
            int dst_idx = (dst_y * dst->width + dst_x) * dst->channels;
            
            for (int c = 0; c < src->channels; c++) {
                dst->data[dst_idx + c] = bilinear_sample(src, sx, sy, c);
            }
        }
    }
}

// ===== Kuyil FFI wrappers =====

Value kyl_path2d_create(int arg_count, Value* args) {
    Path2D* path = path2d_create();
    
    Value result;
    memset(&result, 0, sizeof(Value));
    if (path) {
        result.type = VALUE_NUMBER;
        result.as.number = (double)(uintptr_t)path;
    } else {
        result.type = VALUE_NIL;
    }
    return result;
}

Value kyl_path2d_destroy(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Path2D* path = (Path2D*)(uintptr_t)args[0].as.number;
    path2d_destroy(path);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_path2d_moveTo(int arg_count, Value* args) {
    if (arg_count < 3) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Path2D* path = (Path2D*)(uintptr_t)args[0].as.number;
    double x = args[1].as.number;
    double y = args[2].as.number;
    
    path2d_moveTo(path, x, y);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_path2d_lineTo(int arg_count, Value* args) {
    if (arg_count < 3) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Path2D* path = (Path2D*)(uintptr_t)args[0].as.number;
    double x = args[1].as.number;
    double y = args[2].as.number;
    
    path2d_lineTo(path, x, y);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_path2d_quadraticCurveTo(int arg_count, Value* args) {
    if (arg_count < 5) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Path2D* path = (Path2D*)(uintptr_t)args[0].as.number;
    double cpx = args[1].as.number;
    double cpy = args[2].as.number;
    double x = args[3].as.number;
    double y = args[4].as.number;
    
    path2d_quadraticCurveTo(path, cpx, cpy, x, y);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_path2d_bezierCurveTo(int arg_count, Value* args) {
    if (arg_count < 7) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Path2D* path = (Path2D*)(uintptr_t)args[0].as.number;
    double cp1x = args[1].as.number;
    double cp1y = args[2].as.number;
    double cp2x = args[3].as.number;
    double cp2y = args[4].as.number;
    double x = args[5].as.number;
    double y = args[6].as.number;
    
    path2d_bezierCurveTo(path, cp1x, cp1y, cp2x, cp2y, x, y);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_path2d_stroke(int arg_count, Value* args) {
    if (arg_count < 6) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    Path2D* path = (Path2D*)(uintptr_t)args[1].as.number;
    uint8_t r = (uint8_t)args[2].as.number;
    uint8_t g = (uint8_t)args[3].as.number;
    uint8_t b = (uint8_t)args[4].as.number;
    uint8_t a = arg_count >= 7 ? (uint8_t)args[5].as.number : 255;
    
    path2d_stroke(img, path, r, g, b, a);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_canvas_fillText(int arg_count, Value* args) {
    if (arg_count < 7) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    const char* text = args[1].as.string;
    int32_t x = (int32_t)args[2].as.number;
    int32_t y = (int32_t)args[3].as.number;
    uint8_t r = (uint8_t)args[4].as.number;
    uint8_t g = (uint8_t)args[5].as.number;
    uint8_t b = (uint8_t)args[6].as.number;
    uint8_t a = arg_count >= 8 ? (uint8_t)args[7].as.number : 255;
    
    canvas_fillText(img, text, x, y, r, g, b, a);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_canvas_strokeText(int arg_count, Value* args) {
    if (arg_count < 7) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    const char* text = args[1].as.string;
    int32_t x = (int32_t)args[2].as.number;
    int32_t y = (int32_t)args[3].as.number;
    uint8_t r = (uint8_t)args[4].as.number;
    uint8_t g = (uint8_t)args[5].as.number;
    uint8_t b = (uint8_t)args[6].as.number;
    uint8_t a = arg_count >= 8 ? (uint8_t)args[7].as.number : 255;
    
    canvas_strokeText(img, text, x, y, r, g, b, a);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_canvas_fillTextScaled(int arg_count, Value* args) {
    if (arg_count < 8) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    const char* text = args[1].as.string;
    int32_t x = (int32_t)args[2].as.number;
    int32_t y = (int32_t)args[3].as.number;
    uint8_t r = (uint8_t)args[4].as.number;
    uint8_t g = (uint8_t)args[5].as.number;
    uint8_t b = (uint8_t)args[6].as.number;
    uint8_t a = (uint8_t)args[7].as.number;
    int32_t scale = arg_count >= 9 ? (int32_t)args[8].as.number : 1;
    
    canvas_fillTextScaled(img, text, x, y, r, g, b, a, scale);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_canvas_strokeTextScaled(int arg_count, Value* args) {
    if (arg_count < 8) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* img = (KylImage*)(uintptr_t)args[0].as.number;
    const char* text = args[1].as.string;
    int32_t x = (int32_t)args[2].as.number;
    int32_t y = (int32_t)args[3].as.number;
    uint8_t r = (uint8_t)args[4].as.number;
    uint8_t g = (uint8_t)args[5].as.number;
    uint8_t b = (uint8_t)args[6].as.number;
    uint8_t a = (uint8_t)args[7].as.number;
    int32_t scale = arg_count >= 9 ? (int32_t)args[8].as.number : 1;
    
    canvas_strokeTextScaled(img, text, x, y, r, g, b, a, scale);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_transform_create(int arg_count, Value* args) {
    Transform2D* t = transform_create();
    
    Value result;
    memset(&result, 0, sizeof(Value));
    if (t) {
        result.type = VALUE_NUMBER;
        result.as.number = (double)(uintptr_t)t;
    } else {
        result.type = VALUE_NIL;
    }
    return result;
}

Value kyl_transform_destroy(int arg_count, Value* args) {
    if (arg_count < 1 || args[0].type != VALUE_NUMBER) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Transform2D* t = (Transform2D*)(uintptr_t)args[0].as.number;
    transform_destroy(t);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_transform_translate(int arg_count, Value* args) {
    if (arg_count < 3) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Transform2D* t = (Transform2D*)(uintptr_t)args[0].as.number;
    double x = args[1].as.number;
    double y = args[2].as.number;
    
    transform_translate(t, x, y);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_transform_rotate(int arg_count, Value* args) {
    if (arg_count < 2) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Transform2D* t = (Transform2D*)(uintptr_t)args[0].as.number;
    double angle = args[1].as.number;
    
    transform_rotate(t, angle);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_transform_scale(int arg_count, Value* args) {
    if (arg_count < 3) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    Transform2D* t = (Transform2D*)(uintptr_t)args[0].as.number;
    double sx = args[1].as.number;
    double sy = args[2].as.number;
    
    transform_scale(t, sx, sy);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_canvas_applyTransform(int arg_count, Value* args) {
    if (arg_count < 2) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* src = (KylImage*)(uintptr_t)args[0].as.number;
    Transform2D* t = (Transform2D*)(uintptr_t)args[1].as.number;
    
    KylImage* result_img = canvas_applyTransform(src, t);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    if (result_img) {
        result.type = VALUE_NUMBER;
        result.as.number = (double)(uintptr_t)result_img;
    } else {
        result.type = VALUE_NIL;
    }
    return result;
}

Value kyl_canvas_drawImage(int arg_count, Value* args) {
    if (arg_count < 4) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* dst = (KylImage*)(uintptr_t)args[0].as.number;
    KylImage* src = (KylImage*)(uintptr_t)args[1].as.number;
    int32_t dx = (int32_t)args[2].as.number;
    int32_t dy = (int32_t)args[3].as.number;
    
    canvas_drawImage(dst, src, dx, dy);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_canvas_drawImageScaled(int arg_count, Value* args) {
    if (arg_count < 6) {
        Value nil = {VALUE_NIL};
        return nil;
    }
    
    KylImage* dst = (KylImage*)(uintptr_t)args[0].as.number;
    KylImage* src = (KylImage*)(uintptr_t)args[1].as.number;
    int32_t dx = (int32_t)args[2].as.number;
    int32_t dy = (int32_t)args[3].as.number;
    int32_t dwidth = (int32_t)args[4].as.number;
    int32_t dheight = (int32_t)args[5].as.number;
    
    canvas_drawImageScaled(dst, src, dx, dy, dwidth, dheight);
    
    Value nil = {VALUE_NIL};
    return nil;
}

Value kyl_canvas_measureText(int arg_count, Value* args) {
    if (arg_count < 1) {
        Value result = {VALUE_NUMBER};
        result.as.number = 0;
        return result;
    }
    
    const char* text = args[0].as.string;
    int32_t scale = arg_count >= 2 ? (int32_t)args[1].as.number : 1;
    
    int32_t width = canvas_measureText(text, scale);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)width;
    return result;
}

Value kyl_canvas_textHeight(int arg_count, Value* args) {
    int32_t scale = arg_count >= 1 ? (int32_t)args[0].as.number : 1;
    
    int32_t height = canvas_textHeight(scale);
    
    Value result;
    memset(&result, 0, sizeof(Value));
    result.type = VALUE_NUMBER;
    result.as.number = (double)height;
    return result;
}
