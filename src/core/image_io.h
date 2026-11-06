#pragma once

#include "core/framebuffer.h"

#include <string>
#include <vector>

namespace sr {

bool savePng(const std::string& path, const Framebuffer& fb, ToneMap toneMap, float exposure = 1.0f);
bool savePngRGBA(const std::string& path, int width, int height, const uint8_t* rgba);
// Loads an 8-bit RGBA image (used by golden-image tests).
bool loadRGBA(const std::string& path, int& width, int& height, std::vector<uint8_t>& rgba);

} // namespace sr
