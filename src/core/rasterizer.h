#pragma once
#include <cstdint>
#include "core/framebuffer.h"

void drawLine(Framebuffer& fb, int x0, int y0, int x1, int y1, uint32_t color);