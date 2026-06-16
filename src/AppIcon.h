#ifndef OBSERVER_APP_ICON_H
#define OBSERVER_APP_ICON_H

#include <wx/bitmap.h>
#include <wx/icon.h>

wxBitmap observerAssetBitmap(const char *assetPath, int width, int height);
wxIcon observerAppIcon(int size = 32);

#endif
