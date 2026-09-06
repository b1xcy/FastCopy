#pragma once

#include "ClipboardFileTransfer.h"

#include <filesystem>

[[nodiscard]] bool LaunchFastCopy(
    ClipboardFileTransfer& transfer,
    std::filesystem::path const& destination);
