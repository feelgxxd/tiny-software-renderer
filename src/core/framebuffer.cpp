#include "framebuffer.h"
#include <algorithm>

Framebuffer::Framebuffer(int w, int h)
    : m_width(w), m_height(h)
{
    m_color.resize(w * h);
    m_depth.resize(w * h);
}

void Framebuffer::clear(uint32_t color)
{
    std::fill(m_color.begin(), m_color.end(), color);
}

void Framebuffer::clearDepth(float depth)
{
    std::fill(m_depth.begin(), m_depth.end(), depth);
}

void Framebuffer::setPixel(int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= m_width || y >= m_height)
        return;

    m_color[y * m_width + x] = color;
}

bool Framebuffer::testAndSetDepth(int x, int y, float depth)
{
    if (x < 0 || y < 0 || x >= m_width || y >= m_height)
        return false;

    int index = y * m_width + x;
    if (depth > m_depth[index]) {
        m_depth[index] = depth;
        return true;
    }
    return false;
}