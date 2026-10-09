// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Windows install identity for Shade. Selected by
// chrome/install_static/install_modes.h in place of
// chromium_install_modes.h (patches/0004), and structured the same way.
//
// Everything a second browser could collide with on the same machine is
// unique: the user data and registry path (kCompanyPathName\kProductPathName),
// the app name and AppUserModelID, ProgIDs, the URL scheme, Active Setup and
// the COM class ids. The two interface ids (IElevator, ISystemTraceSession) stay upstream's
// because they are also compiled into the services' IDL files; changing them
// here alone would break those services.

#ifndef GHOST_BRANDING_INSTALL_MODES_H_
#define GHOST_BRANDING_INSTALL_MODES_H_

#include <array>

#include "chrome/app/chrome_dll_resource.h"
#include "chrome/common/chrome_icon_resources_win.h"
#include "chrome/install_static/install_constants.h"

namespace install_static {

// The company directory holds the browser and, beside it, the updater
// (branding/updater.gni uses the same company name). Uninstalling the browser
// clears Software\<company>\<product>, so the updater's
// Software\<company>\Update survives it. No spaces: the updater's own
// uninstall.cmd validates its path with an unquoted FindStr, which splits on
// them (Google and BraveSoftware have none either).
inline constexpr wchar_t kCompanyPathName[] = L"Shade";

inline constexpr wchar_t kProductPathName[] = L"Browser";

// Sent to Google's Safe Browsing service as the client name. Kept at
// upstream's value until the Safe Browsing release gate is decided
// (docs/licensing.md); dev builds do not use the service.
inline constexpr char kSafeBrowsingName[] = "chromium";

enum InstallConstantIndex {
  GHOST_INDEX,
  NUM_INSTALL_MODES,
};

inline constexpr auto kInstallModes = std::to_array<InstallConstants>({
    {
        .size = sizeof(InstallConstants),
        .index = GHOST_INDEX,
        .install_switch = "",
        .install_suffix = L"",
        .logo_suffix = L"",
        // Must equal browser_appid in branding/updater.gni: the key the
        // updater finds this browser under.
        .app_guid = L"{c0ff4371-d9ab-461e-bffd-6b0dc2430b02}",
        .base_app_name = L"Shade",
        .base_app_id = L"Shade",
        // ProgIDs are limited to 39 characters and user-level installs append
        // a 27-character suffix, so prefixes must stay within 12.
        .browser_prog_id_prefix = L"ShadeHTM",
        .browser_prog_id_description = L"Shade HTML Document",
        .direct_launch_url_scheme = "shadebrowser",
        .pdf_prog_id_prefix = L"ShadePDF",
        .pdf_prog_id_description = L"Shade PDF Document",
        .active_setup_guid = L"{4E4F5201-B77F-499F-A2E6-35F0EC9624AD}",
        .toast_activator_clsid = {0x9B478216,
                                  0xBFDD,
                                  0x497A,
                                  {0x99, 0xA4, 0xC3, 0x79, 0x2A, 0xA0, 0xD3,
                                   0x77}},
        .elevator_clsid = {0x7DAD24B8,
                           0x9824,
                           0x484E,
                           {0xAA, 0x08, 0xCB, 0xBF, 0x3A, 0x45, 0x0D, 0x03}},
        // Must match chrome/elevation_service/elevation_service_idl.idl.
        .elevator_iid = {0xbb19a0e5,
                         0xc6,
                         0x4966,
                         {0x94, 0xb2, 0x5a, 0xfe, 0xc6, 0xfe, 0xd9, 0x3a}},
        .old_elevator_iids = {},
        .tracing_service_clsid = {0x42137785,
                                  0x9989,
                                  0x4EA1,
                                  {0x8C, 0x4E, 0x08, 0x1E, 0x24, 0x58, 0xAE,
                                   0x3D}},
        // Must match
        // chrome/windows_services/elevated_tracing_service/tracing_service_idl.idl.
        .tracing_service_iid = {0xe0b03e2d,
                                0x7682,
                                0x4d83,
                                {0xb9, 0xff, 0x45, 0x74, 0xaf, 0x72, 0x05,
                                 0x00}},
        .old_tracing_service_iids = {},
        // One channel, which the registry cannot switch: the empty name is
        // stable (Ghost follows Extended Stable, docs/adr/0003).
        .default_channel_name = L"",
        .channel_strategy = ChannelStrategy::FIXED,
        .supports_system_level = true,
        .supports_set_as_default_browser = true,
        .app_icon_resource_index = icon_resources::kApplicationIndex,
        .app_icon_resource_id = IDR_MAINFRAME,
        .html_doc_icon_resource_index = icon_resources::kHtmlDocIndex,
        .pdf_doc_icon_resource_index = icon_resources::kPDFDocIndex,
        // Distinct from Chromium's so the two browsers' sandboxed processes
        // never share an AppContainer identity.
        .sandbox_sid_prefix = L"S-1-15-2-1573941064-3085085775-2971523722-"
                              L"1643171034-29576539-2045910093-",
    },
});

static_assert(kInstallModes.size() == NUM_INSTALL_MODES,
              "Imbalance between kInstallModes and InstallConstantIndex");

}  // namespace install_static

#endif  // GHOST_BRANDING_INSTALL_MODES_H_
