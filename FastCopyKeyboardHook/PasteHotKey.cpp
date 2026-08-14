#include "PasteHotKey.h"

PasteHotKey::PasteHotKey(HWND window, int id, UINT modifiers, UINT virtualKey)
    : window_{ window }, id_{ id }, modifiers_{ modifiers }, virtualKey_{ virtualKey }
{
}

PasteHotKey::~PasteHotKey()
{
    Unregister();
}

bool PasteHotKey::Register()
{
    registered_ = RegisterHotKey(window_, id_, modifiers_, virtualKey_) != FALSE;
    return registered_;
}

void PasteHotKey::Unregister()
{
    if (registered_)
    {
        UnregisterHotKey(window_, id_);
        registered_ = false;
    }
}
