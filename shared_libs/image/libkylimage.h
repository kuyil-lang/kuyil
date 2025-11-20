#ifndef LIBKYLIMAGE_H
#define LIBKYLIMAGE_H

#include "../c_interface.h"
#include <stdint.h>
#include <stdbool.h>

// Image structure
typedef struct {
    int32_t width;
    int32_t height;
    int32_t channels;  // 1=grayscale, 3=RGB, 4=RGBA
    uint8_t *data;
} KylImage;

// Image loading and saving
KylImage* image_load(const char* filepath);
bool image_save(KylImage* img, const char* filepath, const char* format);
KylImage* image_create(int32_t width, int32_t height, int32_t channels);
void image_destroy(KylImage* img);

// Image information
int32_t image_width(KylImage* img);
int32_t image_height(KylImage* img);
int32_t image_channels(KylImage* img);

// Image manipulation
KylImage* image_resize(KylImage* img, int32_t width, int32_t height, const char* interpolation);
KylImage* image_crop(KylImage* img, int32_t x, int32_t y, int32_t width, int32_t height);
KylImage* image_rotate(KylImage* img, double angle);
KylImage* image_flip(KylImage* img, bool horizontal);

// Pixel operations
void* image_get_pixel(KylImage* img, int32_t x, int32_t y);
void image_set_pixel(KylImage* img, int32_t x, int32_t y, int32_t r, int32_t g, int32_t b, int32_t a);

// Image filters
KylImage* image_grayscale(KylImage* img);
KylImage* image_blur(KylImage* img, double radius);
KylImage* image_sharpen(KylImage* img, double amount);
KylImage* image_brightness(KylImage* img, double factor);
KylImage* image_contrast(KylImage* img, double factor);
KylImage* image_threshold(KylImage* img, int32_t threshold);
KylImage* image_edge_detect(KylImage* img, const char* method);

// Image composition
KylImage* image_blend(KylImage* img1, KylImage* img2, double alpha);
KylImage* image_overlay(KylImage* background, KylImage* foreground, int32_t x, int32_t y);

// Color space conversion
KylImage* image_rgb_to_hsv(KylImage* img);
KylImage* image_hsv_to_rgb(KylImage* img);

#endif // LIBKYLIMAGE_H
