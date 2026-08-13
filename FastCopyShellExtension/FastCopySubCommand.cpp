#include "FastCopySubCommand.h"
#include "CopyOperationNames.h"
#include <Shlwapi.h>
#include "DllIconFormatter.h"
#include "ShellItemArray.h"
#include "Recorder.h"
#include "ShellItem.h"
#include "Registry.h"
#include "FastCopyLauncher.h"
#include "ExplorerFolder.h"
#include <wil/result_macros.h>
#include <Windows.h>
#include <string>
#include <exception>

namespace
{
	std::wstring ToWide(char const* text)
	{
		if (!text)
		{
			return {};
		}
		auto const length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
		std::wstring result(length > 0 ? length - 1 : 0, L'\0');
		if (length > 0)
		{
			MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
		}
		return result;
	}
}

void FastCopySubCommand::recordFilesImpl(IShellItemArray* selection)
{
    Recorder recorder{ m_op };
    for (auto item : ShellItemArray{ selection })
    {
        if (item.Get())
            recorder << item;
    }
}

void FastCopySubCommand::callMainProgramImpl(std::wstring_view arg)
{
    LaunchFastCopy(arg, Registry::Record());
}

FastCopySubCommand::FastCopySubCommand(CopyOperation op) : m_op{ op }
{
}

HRESULT FastCopySubCommand::GetTitle(IShellItemArray*, PWSTR* name)
{
    auto const& nameConstants = CopyOperationNames::GetInstance();
    switch (m_op)
    {
        case CopyOperation::Copy:   return SHStrDup(nameConstants.Copy.data(), name);
        case CopyOperation::Move:   return SHStrDup(nameConstants.Move.data(), name);
        case CopyOperation::Delete: return SHStrDup(nameConstants.Delete.data(), name);
        case CopyOperation::Paste:  return SHStrDup(nameConstants.Paste.data(), name);
    }
}

HRESULT FastCopySubCommand::GetIcon(IShellItemArray*, PWSTR* icon)
{
    return SHStrDup(DllIconFormatter::GetForSubCommand(m_op).data(), icon);
}

HRESULT FastCopySubCommand::GetToolTip(IShellItemArray*, PWSTR* infoTip)
{
    *infoTip = nullptr;
    return E_NOTIMPL;
}

HRESULT FastCopySubCommand::GetCanonicalName(GUID* guidCommandName)
{
    *guidCommandName = GUID_NULL;  
    return E_NOTIMPL;
}

HRESULT FastCopySubCommand::GetState(IShellItemArray* selection, BOOL, EXPCMDSTATE* cmdState)
{
    switch (m_op)
    {
        case CopyOperation::Copy: [[fallthrough]];
        case CopyOperation::Move:
            *cmdState = ShellItemArray{ selection }.size() == 0 ? ECS_DISABLED : ECS_ENABLED;
            break;
        case CopyOperation::Delete:
            *cmdState = ShellItemArray{ selection }.size() == 0 ? ECS_HIDDEN : ECS_ENABLED;
            break;
        case CopyOperation::Paste:
            *cmdState = (Recorder::HasRecord() && (!selection || ShellItemArray{ selection }.size() == 0)) ? 
                ECS_ENABLED : 
                ECS_HIDDEN;
            break;
    }
    return S_OK;
}

void FastCopySubCommand::invokeImpl(IShellItemArray* selection, IBindCtx*)
{
    /*
           if no files are selected, selection contains 1 element to the current invoked folder (Windows 11 only)
           On Windows 10, `selection` is `nullptr`
    */

    switch (m_op)
    {
        case CopyOperation::Copy: [[fallthrough]];
        case CopyOperation::Move: 
            recordFilesImpl(selection);
            return;
        case CopyOperation::Paste:
        {
            //On Windows 11, use selection directly
            if (ShellItemArray shellItemArray{ selection }; shellItemArray.size() != 0)
            {
                ShellItem psi{ shellItemArray[0] };
                callMainProgramImpl(psi.GetDisplayName());
                return;
            }

            // On Windows 10 selection is null, so resolve the active Explorer window.
            // The site is deliberately not retained to avoid a COM reference cycle
            // that kept the COM surrogate alive and made uninstallation slow.
            if (auto folder = GetForegroundExplorerFolder())
            {
                callMainProgramImpl(*folder);
                return;
            }
            break;
        }
        case CopyOperation::Delete:
            recordFilesImpl(selection);
            callMainProgramImpl(L"");
            return;
    }
}

HRESULT FastCopySubCommand::Invoke(IShellItemArray* selection, IBindCtx* ctx)
{
    try
    {
        invokeImpl(selection, ctx);
        return S_OK;
    }
    catch (wil::ResultException const& e)
    {
        MessageBoxW(nullptr, ToWide(e.what()).c_str(), L"RoboCopyEx", MB_OK | MB_ICONERROR);
        return e.GetErrorCode();
    }
    catch (std::exception const& e)
    {
        MessageBoxW(nullptr, ToWide(e.what()).c_str(), L"RoboCopyEx", MB_OK | MB_ICONERROR);
        return E_FAIL;
    }
}

HRESULT FastCopySubCommand::GetFlags(EXPCMDFLAGS* flags)
{
    *flags = ECF_DEFAULT; 
    return S_OK;
}

HRESULT FastCopySubCommand::EnumSubCommands(IEnumExplorerCommand** enumCommands)
{
    *enumCommands = nullptr; 
    return E_NOTIMPL;
}
