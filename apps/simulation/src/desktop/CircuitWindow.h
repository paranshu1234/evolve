#pragma once
#include <windows.h>
#include <filesystem>

namespace evolve::circuit {
// Separate, read-only circuit inspector. The native DNA project is never mutated.
// An empty path opens the packaged example, or asks for a local trace if absent.
void showCircuitWindow(HWND owner, const std::filesystem::path& path = {});
// Route before the DNA workspace's accelerators; true means dispatched/consumed.
bool dispatchCircuitMessage(MSG& message);
// Standalone entry, useful on systems where DX12 is unavailable.
int runCircuitWindow(const std::filesystem::path& path);
}
