#pragma once

namespace msg::branding {

inline constexpr auto kAppName    = L"Mini Message";
inline constexpr auto kAppNameA   = "Mini Message";
inline constexpr auto kDeveloper  = "Wharner APP";
inline constexpr auto kWebsite    = "https://wharner-official-app.tilda.ws/";

// Relative to the executable's working directory (config/ui are copied
// there by the CMake POST_BUILD step, same as image/).
inline constexpr auto kIconRelativePath = L"image/messenger.ico";

} // namespace msg::branding
