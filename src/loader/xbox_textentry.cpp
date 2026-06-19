/*
 * xbox_textentry.cpp - UI Automation submit for the Xbox TextEntryPage popup
 *
 * Verified live (test_popup_probe uia mode): the popup is
 *   [Group] name="TCUI.Pages.TextEntryPage"
 *     [Edit]   name="输入共享代码。"  pat:Value Text   <- SetValue works
 *     [Button] name="确定"            pat:Invoke       <- Invoke works
 *     [Button] name="取消"            pat:Invoke
 * Both patterns work cross-process without stealing foreground focus, so this is
 * how the farm enters share codes (the game window only receives PostMessage,
 * which never reaches this separate UWP process).
 */

#include "xbox_textentry.h"

#include <objbase.h>
#include <uiautomation.h>

/* Find the first descendant matching a Name string. Caller releases result. */
static IUIAutomationElement *FindByName(IUIAutomation *ui,
                                        IUIAutomationElement *scope,
                                        const WCHAR *name) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(name);
    IUIAutomationCondition *cond = nullptr;
    IUIAutomationElement *found = nullptr;
    if (SUCCEEDED(ui->CreatePropertyCondition(UIA_NamePropertyId, v, &cond)) && cond) {
        scope->FindFirst(TreeScope_Descendants, cond, &found);
        cond->Release();
    }
    VariantClear(&v);
    return found;
}

/* Find the first descendant of a given control type. Caller releases result. */
static IUIAutomationElement *FindByControlType(IUIAutomation *ui,
                                               IUIAutomationElement *scope,
                                               CONTROLTYPEID type) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = type;
    IUIAutomationCondition *cond = nullptr;
    IUIAutomationElement *found = nullptr;
    if (SUCCEEDED(ui->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &cond)) && cond) {
        scope->FindFirst(TreeScope_Descendants, cond, &found);
        cond->Release();
    }
    VariantClear(&v);
    return found;
}

static BOOL SetEditValue(IUIAutomation *ui, IUIAutomationElement *scope,
                         const WCHAR *text) {
    IUIAutomationElement *edit = FindByControlType(ui, scope, UIA_EditControlTypeId);
    if (!edit) return FALSE;
    BOOL ok = FALSE;
    IUIAutomationValuePattern *vp = nullptr;
    if (SUCCEEDED(edit->GetCurrentPatternAs(UIA_ValuePatternId,
            __uuidof(IUIAutomationValuePattern), (void **)&vp)) && vp) {
        BSTR b = SysAllocString(text);
        if (SUCCEEDED(vp->SetValue(b))) ok = TRUE;
        SysFreeString(b);
        vp->Release();
    }
    edit->Release();
    return ok;
}

static BOOL InvokeConfirm(IUIAutomation *ui, IUIAutomationElement *scope) {
    /* Confirm button localized name varies by UI language. */
    static const WCHAR *kNames[] = { L"确定", L"確定", L"OK", L"Confirm" };
    for (int i = 0; i < (int)(sizeof(kNames) / sizeof(kNames[0])); i++) {
        IUIAutomationElement *btn = FindByName(ui, scope, kNames[i]);
        if (!btn) continue;
        BOOL ok = FALSE;
        IUIAutomationInvokePattern *ip = nullptr;
        if (SUCCEEDED(btn->GetCurrentPatternAs(UIA_InvokePatternId,
                __uuidof(IUIAutomationInvokePattern), (void **)&ip)) && ip) {
            if (SUCCEEDED(ip->Invoke())) ok = TRUE;
            ip->Release();
        }
        btn->Release();
        if (ok) return TRUE;
    }
    return FALSE;
}

extern "C" BOOL XboxTextEntry_SubmitShareCode(const char *code, int timeout_ms) {
    if (!code || !code[0]) return FALSE;

    WCHAR wcode[64];
    if (MultiByteToWideChar(CP_UTF8, 0, code, -1, wcode, 64) <= 0)
        return FALSE;

    HRESULT hrco = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BOOL did_init = (hrco == S_OK || hrco == S_FALSE);

    IUIAutomation *ui = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IUIAutomation, (void **)&ui);
    BOOL ok = FALSE;
    if (SUCCEEDED(hr) && ui) {
        DWORD start = GetTickCount();
        if (timeout_ms < 500) timeout_ms = 500;
        do {
            IUIAutomationElement *root = nullptr;
            if (SUCCEEDED(ui->GetRootElement(&root)) && root) {
                /* Prefer scoping to the TextEntryPage group; fall back to root. */
                IUIAutomationElement *page =
                    FindByName(ui, root, L"TCUI.Pages.TextEntryPage");
                IUIAutomationElement *scope = page ? page : root;

                if (SetEditValue(ui, scope, wcode) && InvokeConfirm(ui, scope))
                    ok = TRUE;

                if (page) page->Release();
                root->Release();
            }
            if (ok) break;
            Sleep(300);
        } while ((int)(GetTickCount() - start) < timeout_ms);
        ui->Release();
    }

    if (did_init) CoUninitialize();
    return ok;
}
