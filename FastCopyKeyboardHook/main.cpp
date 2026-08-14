#include "KeyboardHookApp.h"

#include <wil/result_macros.h>

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    try
    {
        KeyboardHookApp app{ instance };
        return app.Run();
    }
    catch (wil::ResultException const& e)
    {
        KeyboardHookApp::ShowError(e.what());
        return 1;
    }
    catch (std::exception const& e)
    {
        KeyboardHookApp::ShowError(e.what());
        return 1;
    }
}
