#pragma once

#include "ClipboardFileTransfer.h"

#include <filesystem>

[[nodiscard]] bool LaunchFastCopy(
    ClipboardFileTransfer const& transfer,
    std::filesystem::path const& destination);
