// Resource identifiers for falldesktop.rc
#pragma once

#define IDI_APP             101
#define IDI_APP_PAUSED      102

// Pixel-art leaf bitmaps (PNG, RCDATA), consecutive ids.
#define IDR_PIXEL_FIRST   201
#define IDR_PIXEL1        201
#define IDR_PIXEL2        202
#define IDR_PIXEL3        203
#define IDR_PIXEL4        204
#define IDR_PIXEL5        205
#define IDR_PIXEL6        206
#define IDR_PIXEL7        207
#define IDR_PIXEL8        208

// The version comes from CMake (project VERSION, overridden by CI from the
// release tag); these defaults only apply to builds outside CMake.
#ifndef FALLDESKTOP_VERSION_MAJOR
#define FALLDESKTOP_VERSION_MAJOR 0
#define FALLDESKTOP_VERSION_MINOR 0
#define FALLDESKTOP_VERSION_PATCH 0
#define FALLDESKTOP_VERSION_STR   "0.0.0-dev"
#endif
