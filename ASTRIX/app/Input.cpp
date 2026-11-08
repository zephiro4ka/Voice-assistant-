#include "Input.h"

#include "TextUtils.h"

#include <vector>

namespace {
    INPUT KeyEvent(WORD key, bool up) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = key;
        input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        // стрелки, Win и т.п. — «расширенные» клавиши
        switch (key) {
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_LWIN: case VK_RWIN:
        case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT: case VK_INSERT: case VK_DELETE:
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            break;
        }
        return input;
    }

    INPUT UnicodeEvent(wchar_t ch, bool up) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = ch;
        input.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0);
        return input;
    }

    void Send(std::vector<INPUT>& inputs) {
        if (!inputs.empty()) SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }
}

void Input::TypeText(const std::string& utf8) {
    std::vector<INPUT> inputs;
    for (wchar_t ch : Text::ToWide(utf8)) {
        if (ch == L'\n') {
            inputs.push_back(KeyEvent(VK_RETURN, false));
            inputs.push_back(KeyEvent(VK_RETURN, true));
            continue;
        }
        inputs.push_back(UnicodeEvent(ch, false));
        inputs.push_back(UnicodeEvent(ch, true));
    }
    Send(inputs);
}

void Input::Chord(std::initializer_list<WORD> keys) {
    std::vector<INPUT> inputs;
    for (WORD key : keys) inputs.push_back(KeyEvent(key, false));
    for (auto it = keys.end(); it != keys.begin();) inputs.push_back(KeyEvent(*--it, true));
    Send(inputs);
}

void Input::Tap(WORD key, int times) {
    std::vector<INPUT> inputs;
    for (int i = 0; i < times; ++i) {
        inputs.push_back(KeyEvent(key, false));
        inputs.push_back(KeyEvent(key, true));
    }
    Send(inputs);
}
