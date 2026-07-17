#pragma once

namespace hsrc::sdk::settings {

bool open();
bool open_category(const char *id);
bool open_deeplink(const char *uri);

} // namespace hsrc::sdk::settings
