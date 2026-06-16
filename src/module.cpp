/*
 * Copyright (C) 2026 Lenik <observer@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include <bas/log/deflog.h>
#include <bas/proc/AssetsRegistry.hpp>
#include <bas/proc/DefAssets.hpp>

define_logger();
define_zip_assets(observer, observer_assets);

struct ObserverAssetsRegistrar {
    ObserverAssetsRegistrar() { //
        AssetsRegistry::pushLayer(observer_assets.get());
    }
} observer_assets_registrar;
