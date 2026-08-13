#pragma once
#include <string_view>

// Launches the main FastCopy program through the fastcopy:// protocol with the
// given destination folder and record file path.
// Throws wil::ResultException if launching fails.
void LaunchFastCopy(std::wstring_view destination, std::wstring_view recordFile);
