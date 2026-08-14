#pragma once
#include <Windows.h>

// Registers a hot key on a window and unregisters it on destruction (RAII).
// The hot key can be temporarily unregistered (while a native paste is being
// replayed) and registered again afterwards.
class PasteHotKey
{
public:
    PasteHotKey(HWND window, int id, UINT modifiers, UINT virtualKey);
    ~PasteHotKey();

    PasteHotKey(PasteHotKey const&) = delete;
    PasteHotKey& operator=(PasteHotKey const&) = delete;

    bool Register();
    void Unregister();
    bool registered() const { return registered_; }
    int id() const { return id_; }

private:
    HWND window_{};
    int id_{};
    UINT modifiers_{};
    UINT virtualKey_{};
    bool registered_{};
};
