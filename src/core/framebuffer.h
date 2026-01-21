#pragma once
#include <vector>
#include <cstdint>

class Framebuffer {
public:
    Framebuffer(int w, int h);

    void clear(uint32_t color);
    void clearDepth(float depth);

    void setPixel(int x, int y, uint32_t color);

    bool testAndSetDepth(int x, int y, float depth);

    int width() const { return m_width; }
    int height() const { return m_height; }

    const uint32_t* data() const { return m_color.data(); }

private:
    int m_width;
    int m_height;

    std::vector<uint32_t> m_color;
    std::vector<float> m_depth;
};