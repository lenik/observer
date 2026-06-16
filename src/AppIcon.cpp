#include "AppIcon.h"

#include <bas/proc/AssetsRegistry.hpp>
#include <bas/ui/arch/ImageSet.hpp>

#include <wx/image.h>

#include <algorithm>

namespace {

bool isBackdropPixel(unsigned char r, unsigned char g, unsigned char b)
{
    const unsigned char minChannel = std::min({r, g, b});
    const unsigned char maxChannel = std::max({r, g, b});
    return minChannel >= 235 && (maxChannel - minChannel) <= 12;
}

wxImage trimBackdrop(const wxImage& source)
{
    if (!source.IsOk()) {
        return source;
    }

    const int width = source.GetWidth();
    const int height = source.GetHeight();
    const unsigned char* data = source.GetData();

    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int idx = (y * width + x) * 3;
            const unsigned char r = data[idx];
            const unsigned char g = data[idx + 1];
            const unsigned char b = data[idx + 2];
            if (!isBackdropPixel(r, g, b)) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (maxX < minX || maxY < minY) {
        return source;
    }

    const wxRect crop(minX, minY, maxX - minX + 1, maxY - minY + 1);
    return source.GetSubImage(crop);
}

wxImage applyTrayAlpha(const wxImage& source)
{
    wxImage image = source;
    if (!image.IsOk()) {
        return image;
    }

    if (!image.HasAlpha()) {
        image.InitAlpha();
    }

    unsigned char* alpha = image.GetAlpha();
    unsigned char* data = image.GetData();
    const int width = image.GetWidth();
    const int height = image.GetHeight();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int idx = y * width + x;
            const int dataIdx = idx * 3;
            const unsigned char r = data[dataIdx];
            const unsigned char g = data[dataIdx + 1];
            const unsigned char b = data[dataIdx + 2];
            alpha[idx] = isBackdropPixel(r, g, b) ? 0 : 255;
        }
    }

    return image;
}

wxBitmap toTrayBitmap(const wxBitmap& source, int size)
{
    wxImage image = source.ConvertToImage();
    if (!image.IsOk()) {
        return source;
    }

    image = trimBackdrop(image);
    image.Rescale(size, size, wxIMAGE_QUALITY_HIGH);
    image = applyTrayAlpha(image);

    return wxBitmap(image, 32);
}

} // namespace

wxBitmap observerAssetBitmap(const char *assetPath, int width, int height) {
    auto volume = AssetsRegistry::instance().get();
    if (volume == nullptr) {
        return wxBitmap();
    }

    ImageSet image{Path(assetPath)};
    image.detect(volume);

    BitmapMode mode;
    mode.assets_preferred = true;
    mode.no_stockart = true;

    const BitmapResult result = image.toBitmap(width, height, wxART_OTHER, mode);
    if (result.IsOk()) {
        return result;
    }
    return wxBitmap();
}

wxIcon observerAppIcon(int size) {
    wxIcon icon;
    const int loadSize = std::max(size, 128);
    const wxBitmap bitmap = observerAssetBitmap("icon-256.png", loadSize, loadSize);
    if (!bitmap.IsOk()) {
        return icon;
    }

    icon.CopyFromBitmap(toTrayBitmap(bitmap, size));
    return icon;
}
