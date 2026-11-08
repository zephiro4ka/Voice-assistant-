#include "ScreenLinks.h"

#include "Platform.h"
#include "Browser.h"
#include "Com.h"
#include "Fuzzy.h"
#include "Input.h"
#include "Log.h"
#include "TextUtils.h"

#include <UIAutomation.h>

#include <initializer_list>
#include <utility>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace {
    constexpr float kMatchThreshold = 0.6f; // насколько название ссылки должно совпасть со сказанным

    struct Candidate {
        Com<IUIAutomationElement> element;
        std::string name;
        float score = 0.0f;
    };

    std::string ElementName(IUIAutomationElement* element) {
        BSTR name = nullptr;
        if (FAILED(element->get_CurrentName(&name)) || !name) return {};
        std::string utf8 = Text::ToUtf8(name);
        SysFreeString(name);
        return utf8;
    }

    // Все ссылки на странице; браузер строит дерево доступности по первому запросу — поэтому вторая попытка
    bool FindBestLink(IUIAutomation* automation, HWND window, const std::string& spoken, Candidate& best) {
        Com<IUIAutomationElement> root;
        if (FAILED(automation->ElementFromHandle(window, &root)) || !root) return false;
        VARIANT type;
        type.vt = VT_I4;
        type.lVal = UIA_HyperlinkControlTypeId;
        Com<IUIAutomationCondition> condition;
        automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &condition);

        for (int attempt = 0; attempt < 2; ++attempt) {
            Com<IUIAutomationElementArray> links;
            if (SUCCEEDED(root->FindAll(TreeScope_Descendants, condition.p, &links)) && links) {
                int count = 0;
                links->get_Length(&count);
                wchar_t title[256] = {};
                GetWindowTextW(window, title, 256);
                Log::Write("ссылки на экране: окно «" + Text::ToUtf8(title) + "», ссылок " + std::to_string(count));
                for (int i = 0; i < count; ++i) {
                    IUIAutomationElement* link = nullptr;
                    if (FAILED(links->GetElement(i, &link)) || !link) continue;
                    const std::string name = ElementName(link);
                    BOOL offscreen = FALSE;
                    link->get_CurrentIsOffscreen(&offscreen);
                    // то, что видно на экране, важнее того, что ниже по странице
                    const float score = name.size() < 3 ? 0.0f : Fuzzy::TitleMatch(spoken, name) - (offscreen ? 0.05f : 0.0f);
                    if (score > best.score) {
                        if (best.element.p) best.element.p->Release();
                        best.element.p = link;
                        best.name = name;
                        best.score = score;
                    } else {
                        link->Release();
                    }
                }
                if (count > 0) return true;
            }
            Sleep(700);
        }
        return false;
    }

    void ClickCenter(IUIAutomationElement* element) {
        RECT r{};
        if (FAILED(element->get_CurrentBoundingRectangle(&r))) return;
        const int x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
        const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        const int left = GetSystemMetrics(SM_XVIRTUALSCREEN), top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        INPUT clicks[3] = {};
        for (auto& c : clicks) c.type = INPUT_MOUSE;
        clicks[0].mi.dx = static_cast<LONG>((x - left) * 65535.0 / (w - 1));
        clicks[0].mi.dy = static_cast<LONG>((y - top) * 65535.0 / (h - 1));
        clicks[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        clicks[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        clicks[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(3, clicks, sizeof(INPUT));
    }

    // Нажать ссылку: штатно (Invoke), а если браузер не умеет — кликом мышью по её центру
    void Activate(IUIAutomationElement* element) {
        element->SetFocus();
        Com<IUIAutomationInvokePattern> invoke;
        if (SUCCEEDED(element->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), reinterpret_cast<void**>(&invoke))) &&
            invoke && SUCCEEDED(invoke->Invoke()))
            return;
        ClickCenter(element);
    }
}

namespace {
    // Найти лучшую ссылку; если click — ещё и нажать её
    ScreenLinks::Result Run(const std::string& spokenTitle, bool click) {
        ComScope com;
        const HWND browser = Browser::Window(true); // свёрнутую страницу Windows не отдаёт — разворачиваем
        if (!browser) return {};

        Com<IUIAutomation> automation;
        if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                reinterpret_cast<void**>(&automation))) || !automation)
            return {};

        Candidate best;
        FindBestLink(automation.p, browser, spokenTitle, best);
        Log::Write("ссылки на экране: лучшая «" + best.name + "», похожесть " + std::to_string(best.score));
        if (!best.element || best.score < kMatchThreshold) return {};

        if (click) {
            Browser::Focus();
            Activate(best.element.p);
        }
        return { true, best.name };
    }
}

namespace {
    struct Page {
        ComScope com;
        Com<IUIAutomation> automation;
        Com<IUIAutomationElement> document; // сама страница — без вкладок, адресной строки и панели закладок
    };

    bool OpenPage(Page& page) {
        const HWND browser = Browser::Window(true);
        if (!browser) return false;
        if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                reinterpret_cast<void**>(&page.automation))) || !page.automation)
            return false;
        Com<IUIAutomationElement> root;
        if (FAILED(page.automation->ElementFromHandle(browser, &root)) || !root) return false;
        VARIANT type;
        type.vt = VT_I4;
        type.lVal = UIA_DocumentControlTypeId;
        Com<IUIAutomationCondition> condition;
        page.automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &condition);
        for (int attempt = 0; attempt < 2 && !page.document; ++attempt) { // дерево доступности строится по первому запросу
            root->FindFirst(TreeScope_Descendants, condition.p, &page.document);
            if (!page.document) Sleep(500);
        }
        return page.document.p != nullptr;
    }

    void FindAllOfType(Page& page, std::initializer_list<CONTROLTYPEID> types, Com<IUIAutomationElementArray>& found) {
        Com<IUIAutomationCondition> any;
        for (CONTROLTYPEID id : types) {
            VARIANT type;
            type.vt = VT_I4;
            type.lVal = id;
            Com<IUIAutomationCondition> one;
            page.automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &one);
            if (!any) { any.p = one.p; one.p = nullptr; continue; }
            Com<IUIAutomationCondition> both;
            page.automation->CreateOrCondition(any.p, one.p, &both);
            any.p->Release();
            any.p = both.p;
            both.p = nullptr;
        }
        page.document->FindAll(TreeScope_Descendants, any.p, &found);
    }

    bool IsHeading(IUIAutomationElement* element) {
        VARIANT level;
        VariantInit(&level);
        const bool heading = SUCCEEDED(element->GetCurrentPropertyValue(UIA_HeadingLevelPropertyId, &level)) && level.vt == VT_I4 &&
                             level.lVal != HeadingLevel_None;
        VariantClear(&level);
        return heading;
    }

    // Ссылка-заголовок результата: <a><h3>…</h3></a> (Google, Яндекс) или <h3><a>…</a></h3> (Bing, YouTube)
    bool IsResultLink(Page& page, IUIAutomationElement* link) {
        Com<IUIAutomationTreeWalker> walker;
        page.automation->get_RawViewWalker(&walker);
        Com<IUIAutomationElement> parent;
        if (walker && SUCCEEDED(walker->GetParentElement(link, &parent)) && parent && IsHeading(parent.p)) return true;
        Com<IUIAutomationCondition> isNone, notNone;
        VARIANT none;
        none.vt = VT_I4;
        none.lVal = HeadingLevel_None;
        page.automation->CreatePropertyCondition(UIA_HeadingLevelPropertyId, none, &isNone);
        page.automation->CreateNotCondition(isNone.p, &notNone);
        Com<IUIAutomationElement> heading;
        return SUCCEEDED(link->FindFirst(TreeScope_Descendants, notNone.p, &heading)) && heading;
    }

    std::string Lower(const std::string& text) { return Text::ToUtf8(Text::LowerRu(Text::ToWide(text))); }
}

ScreenLinks::Result ScreenLinks::OpenNth(int n) {
    Page page;
    if (n < 1 || !OpenPage(page)) return {};
    Com<IUIAutomationElementArray> links;
    FindAllOfType(page, { UIA_HyperlinkControlTypeId }, links);
    int count = 0;
    if (links) links->get_Length(&count);

    Com<IUIAutomationElement> chosen;
    std::string chosenName;
    std::vector<std::pair<int, std::string>> longLinks; // запасной вариант: длинные видимые ссылки (номер в списке и название)
    int results = 0;
    for (int i = 0; i < count && !chosen; ++i) {
        Com<IUIAutomationElement> link;
        if (FAILED(links->GetElement(i, &link)) || !link) continue;
        const std::string name = Text::Trim(ElementName(link.p));
        if (name.size() < 3) continue;
        if (IsResultLink(page, link.p)) {
            if (++results == n) { chosen.p = link.p; link.p = nullptr; chosenName = name; }
            continue;
        }
        BOOL offscreen = FALSE;
        link->get_CurrentIsOffscreen(&offscreen);
        if (!offscreen && Text::ToWide(name).size() >= 15 && longLinks.size() < 20) longLinks.emplace_back(i, name);
    }
    if (!chosen && results == 0 && static_cast<int>(longLinks.size()) >= n) {
        links->GetElement(longLinks[n - 1].first, &chosen);
        chosenName = longLinks[n - 1].second;
    }
    Log::Write("ссылка №" + std::to_string(n) + ": ссылок " + std::to_string(count) + ", результатов " + std::to_string(results) +
               (chosen ? ", жму «" + chosenName + "»" : ", не нашла"));
    if (!chosen) return {};
    Browser::Focus();
    Activate(chosen.p);
    return { true, chosenName };
}

bool ScreenLinks::FocusSearchField() {
    Page page;
    if (!OpenPage(page)) return false;
    Com<IUIAutomationElementArray> fields;
    FindAllOfType(page, { UIA_EditControlTypeId, UIA_ComboBoxControlTypeId }, fields);
    int count = 0;
    if (fields) fields->get_Length(&count);

    Com<IUIAutomationElement> best, firstVisible;
    for (int i = 0; i < count && !best; ++i) {
        Com<IUIAutomationElement> field;
        if (FAILED(fields->GetElement(i, &field)) || !field) continue;
        BOOL offscreen = FALSE, enabled = TRUE;
        field->get_CurrentIsOffscreen(&offscreen);
        field->get_CurrentIsEnabled(&enabled);
        if (offscreen || !enabled) continue;
        const std::string name = Lower(ElementName(field.p));
        if (name.find("поиск") != std::string::npos || name.find("search") != std::string::npos || name.find("запрос") != std::string::npos ||
            name.find("найти") != std::string::npos) {
            best.p = field.p;
            field.p = nullptr;
        } else if (!firstVisible) {
            firstVisible.p = field.p;
            field.p = nullptr;
        }
    }
    IUIAutomationElement* target = best ? best.p : firstVisible.p;
    Log::Write("поле поиска на странице: полей " + std::to_string(count) + (target ? ", нашла «" + ElementName(target) + "»" : ", нет"));
    if (!target) return false;
    Browser::Focus();
    if (FAILED(target->SetFocus())) ClickCenter(target);
    return true;
}

ScreenLinks::Result ScreenLinks::Find(const std::string& spokenTitle) { return Run(spokenTitle, false); }
ScreenLinks::Result ScreenLinks::Open(const std::string& spokenTitle) { return Run(spokenTitle, true); }
