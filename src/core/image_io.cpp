#include "core/image_io.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_image.h>
#include <stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <filesystem>

namespace sr {

bool savePngRGBA(const std::string& path, int width, int height, const uint8_t* rgba)
{
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    return stbi_write_png(path.c_str(), width, height, 4, rgba, width * 4) != 0;
}

bool savePng(const std::string& path, const Framebuffer& fb, ToneMap toneMap, float exposure)
{
    std::vector<uint8_t> rgba(static_cast<size_t>(fb.width()) * fb.height() * 4);
    fb.resolveRGBA(rgba.data(), toneMap, exposure);
    return savePngRGBA(path, fb.width(), fb.height(), rgba.data());
}

bool loadRGBA(const std::string& path, int& width, int& height, std::vector<uint8_t>& rgba)
{
    int channels = 0;
    stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) return false;
    rgba.assign(data, data + static_cast<size_t>(width) * height * 4);
    stbi_image_free(data);
    return true;
}

} // namespace sr
