#include "ClipboardFileTransfer.h"

#include <ole2.h>

#include <wil/resource.h>

#include <ShlObj_core.h>
#include <ShObjIdl_core.h>
#include <shellapi.h>
#include <wrl/client.h>

namespace
{
    using Microsoft::WRL::ComPtr;

    std::optional<DWORD> GetPreferredDropEffect(IDataObject* dataObject)
    {
        FORMATETC format{};
        format.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT));
        format.dwAspect = DVASPECT_CONTENT;
        format.lindex = -1;
        format.tymed = TYMED_HGLOBAL;

        STGMEDIUM medium{};
        if (FAILED(dataObject->GetData(&format, &medium)))
        {
            return std::nullopt;
        }

        std::optional<DWORD> effect;
        if (medium.tymed == TYMED_HGLOBAL && GlobalSize(medium.hGlobal) >= sizeof(DWORD))
        {
            auto const value = static_cast<DWORD const*>(GlobalLock(medium.hGlobal));
            if (value)
            {
                effect = *value;
                GlobalUnlock(medium.hGlobal);
            }
        }
        ReleaseStgMedium(&medium);
        return effect;
    }

    std::optional<std::vector<wil::unique_cotaskmem_string>> ReadShellItems(IDataObject* dataObject)
    {
        ComPtr<IShellItemArray> items;
        if (FAILED(SHCreateShellItemArrayFromDataObject(dataObject, IID_PPV_ARGS(&items))))
        {
            return std::nullopt;
        }

        DWORD itemCount{};
        if (FAILED(items->GetCount(&itemCount)) || itemCount == 0)
        {
            return std::nullopt;
        }

        std::vector<wil::unique_cotaskmem_string> paths;
        paths.reserve(itemCount);
        for (DWORD index = 0; index < itemCount; ++index)
        {
            ComPtr<IShellItem> item;
            wil::unique_cotaskmem_string path;
            if (FAILED(items->GetItemAt(index, &item)) ||
                FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, wil::out_param(path))))
            {
                return std::nullopt;
            }
            paths.push_back(std::move(path));
        }
        return paths;
    }

    std::optional<std::vector<wil::unique_cotaskmem_string>> ReadDropFiles(IDataObject* dataObject)
    {
        FORMATETC format{};
        format.cfFormat = CF_HDROP;
        format.dwAspect = DVASPECT_CONTENT;
        format.lindex = -1;
        format.tymed = TYMED_HGLOBAL;

        STGMEDIUM medium{};
        if (FAILED(dataObject->GetData(&format, &medium)))
        {
            return std::nullopt;
        }

        auto const drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
        if (!drop)
        {
            ReleaseStgMedium(&medium);
            return std::nullopt;
        }

        std::vector<wil::unique_cotaskmem_string> paths;
        auto const count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        paths.reserve(count);
        for (UINT index = 0; index < count; ++index)
        {
            auto const length = DragQueryFileW(drop, index, nullptr, 0);
            if (length == 0)
            {
                paths.clear();
                break;
            }
            wil::unique_cotaskmem_string path(static_cast<PWSTR>(CoTaskMemAlloc((length + 1) * sizeof(wchar_t))));
            if (!path || DragQueryFileW(drop, index, path.get(), length + 1) != length)
            {
                paths.clear();
                break;
            }
            paths.push_back(std::move(path));
        }

        GlobalUnlock(medium.hGlobal);
        ReleaseStgMedium(&medium);
        return paths.empty() ? std::nullopt : std::optional{ std::move(paths) };
    }
}

std::optional<ClipboardFileTransfer> ClipboardFileTransfer::Read()
{
    ComPtr<IDataObject> dataObject;
    if (FAILED(OleGetClipboard(&dataObject)))
    {
        return std::nullopt;
    }

    ClipboardFileTransfer transfer;
    if (auto shellPaths = ReadShellItems(dataObject.Get()))
    {
        transfer.paths = std::move(*shellPaths);
    }
    else if (auto dropPaths = ReadDropFiles(dataObject.Get()))
    {
        transfer.paths = std::move(*dropPaths);
    }
    else
    {
        return std::nullopt;
    }

    if (auto const effect = GetPreferredDropEffect(dataObject.Get()))
    {
        transfer.move = (*effect & DROPEFFECT_MOVE) != 0 && (*effect & DROPEFFECT_COPY) == 0;
    }

    return transfer;
}
