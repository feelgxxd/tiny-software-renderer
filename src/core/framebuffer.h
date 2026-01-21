#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <limits>

class Framebuffer
{
public:
    Framebuffer(int width, int height)
        : m_width(width),
        m_height(height),
        m_colorBuffer(width * height, 0xFF000000),
        m_depthBuffer(width * height, std::numeric_limits<float>::infinity())
    {}

    std::vector<float> m_depthBuffer;

    int width() const { return m_width; }
    int height() const { return m_height; }

    uint32_t* data() { return m_colorBuffer.data(); }
    const uint32_t* data() const { return m_colorBuffer.data(); }

    void clear(uint32_t color)
    {
        std::fill(m_colorBuffer.begin(), m_colorBuffer.end(), color);
        std::fill(m_depthBuffer.begin(), m_depthBuffer.end(),
                std::numeric_limits<float>::infinity());
    }

    inline void setPixel(int x, int y, uint32_t color)
    {
        if (x < 0 || y < 0 || x >= m_width || y >= m_height)
            return;

        m_colorBuffer[y * m_width + x] = color;
    }

    float& depth(int x, int y)
    {
        return m_depthBuffer[y * m_width + x];
    }

private:
    int m_width;
    int m_height;
    std::vector<uint32_t> m_colorBuffer;
};