#include "platform/FileDialog.hpp"

#include "core/Log.hpp"

#include <windows.h>
#include <commdlg.h>
#include <iterator>

namespace vr::platform {

std::optional<std::filesystem::path> openModelDialog(const std::optional<std::filesystem::path>& recentPath) {
    wchar_t path[32768]{};
    constexpr wchar_t filters[] =
        L"3D models\0*.gltf;*.glb;*.obj;*.fbx;*.dae;*.3ds;*.ply;*.stl;*.blend;*.x\0"
        L"glTF 2.0\0*.gltf;*.glb\0"
        L"All files\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrFilter = filters;
    dialog.nFilterIndex = 1;
    std::wstring initialDirectory;
    if (recentPath && !recentPath->empty()) {
        initialDirectory = recentPath->parent_path().wstring();
        if (!initialDirectory.empty()) dialog.lpstrInitialDir = initialDirectory.c_str();
    }
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    dialog.lpstrTitle = L"Open 3D Model";
    if (GetOpenFileNameW(&dialog) == FALSE) {
        const DWORD error = CommDlgExtendedError();
        if (error != 0) Logger::instance().error("File Dialog", "Windows file picker failed with error " + std::to_string(error) + ".");
        return std::nullopt;
    }
    return std::filesystem::path(path);
}

} // namespace vr::platform
