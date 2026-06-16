#include "AppIcon.h"

#include <bas/proc/AssetsRegistry.hpp>
#include <bas/ui/arch/ImageSet.hpp>

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
    const wxBitmap bitmap = observerAssetBitmap("icon-light.png", size, size);
    if (bitmap.IsOk()) {
        icon.CopyFromBitmap(bitmap);
    }
    return icon;
}
