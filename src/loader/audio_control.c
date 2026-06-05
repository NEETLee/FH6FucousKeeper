/*
 * audio_control.c - Process Audio Mute Control (WASAPI COM)
 *
 * Enumerates audio sessions to find the target process,
 * then uses ISimpleAudioVolume to mute/unmute it.
 *
 * COM interfaces are accessed through vtable pointers for C compatibility.
 */

#include "audio_control.h"
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>

static BOOL s_com_initialized = FALSE;

/* ─── Public API ──────────────────────────────────────────────────── */

BOOL AudioCtrl_Init(void)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr) || hr == S_FALSE) {
        s_com_initialized = TRUE;
        return TRUE;
    }
    /* RPC_E_CHANGED_MODE means COM is already initialized with different model - still OK */
    if (hr == RPC_E_CHANGED_MODE) {
        s_com_initialized = FALSE; /* Don't uninitialize what we didn't initialize */
        return TRUE;
    }
    return FALSE;
}

void AudioCtrl_Shutdown(void)
{
    if (s_com_initialized) {
        CoUninitialize();
        s_com_initialized = FALSE;
    }
}

BOOL AudioCtrl_MuteProcess(DWORD pid, BOOL mute)
{
    IMMDeviceEnumerator *pEnumerator = NULL;
    IMMDevice *pDevice = NULL;
    IAudioSessionManager2 *pSessionMgr = NULL;
    IAudioSessionEnumerator *pSessionEnum = NULL;
    BOOL result = FALSE;
    int count = 0;

    HRESULT hr = CoCreateInstance(
        &CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
        &IID_IMMDeviceEnumerator, (void **)&pEnumerator);
    if (FAILED(hr)) goto cleanup;

    hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(
        pEnumerator, eRender, eMultimedia, &pDevice);
    if (FAILED(hr)) goto cleanup;

    hr = pDevice->lpVtbl->Activate(
        pDevice, &IID_IAudioSessionManager2, CLSCTX_ALL, NULL, (void **)&pSessionMgr);
    if (FAILED(hr)) goto cleanup;

    hr = pSessionMgr->lpVtbl->GetSessionEnumerator(pSessionMgr, &pSessionEnum);
    if (FAILED(hr)) goto cleanup;

    hr = pSessionEnum->lpVtbl->GetCount(pSessionEnum, &count);
    if (FAILED(hr)) goto cleanup;

    for (int i = 0; i < count; i++) {
        IAudioSessionControl *pCtrl = NULL;
        IAudioSessionControl2 *pCtrl2 = NULL;
        ISimpleAudioVolume *pVolume = NULL;
        DWORD sessionPid = 0;

        hr = pSessionEnum->lpVtbl->GetSession(pSessionEnum, i, &pCtrl);
        if (FAILED(hr)) continue;

        hr = pCtrl->lpVtbl->QueryInterface(pCtrl, &IID_IAudioSessionControl2, (void **)&pCtrl2);
        if (FAILED(hr)) { pCtrl->lpVtbl->Release(pCtrl); continue; }

        hr = pCtrl2->lpVtbl->GetProcessId(pCtrl2, &sessionPid);
        if (SUCCEEDED(hr) && sessionPid == pid) {
            hr = pCtrl->lpVtbl->QueryInterface(pCtrl, &IID_ISimpleAudioVolume, (void **)&pVolume);
            if (SUCCEEDED(hr)) {
                pVolume->lpVtbl->SetMute(pVolume, mute, NULL);
                pVolume->lpVtbl->Release(pVolume);
                result = TRUE;
            }
        }

        pCtrl2->lpVtbl->Release(pCtrl2);
        pCtrl->lpVtbl->Release(pCtrl);

        if (result) break;
    }

cleanup:
    if (pSessionEnum) pSessionEnum->lpVtbl->Release(pSessionEnum);
    if (pSessionMgr) pSessionMgr->lpVtbl->Release(pSessionMgr);
    if (pDevice) pDevice->lpVtbl->Release(pDevice);
    if (pEnumerator) pEnumerator->lpVtbl->Release(pEnumerator);

    return result;
}

BOOL AudioCtrl_IsProcessMuted(DWORD pid)
{
    IMMDeviceEnumerator *pEnumerator = NULL;
    IMMDevice *pDevice = NULL;
    IAudioSessionManager2 *pSessionMgr = NULL;
    IAudioSessionEnumerator *pSessionEnum = NULL;
    BOOL muted = FALSE;
    int count = 0;

    HRESULT hr = CoCreateInstance(
        &CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
        &IID_IMMDeviceEnumerator, (void **)&pEnumerator);
    if (FAILED(hr)) goto cleanup;

    hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(
        pEnumerator, eRender, eMultimedia, &pDevice);
    if (FAILED(hr)) goto cleanup;

    hr = pDevice->lpVtbl->Activate(
        pDevice, &IID_IAudioSessionManager2, CLSCTX_ALL, NULL, (void **)&pSessionMgr);
    if (FAILED(hr)) goto cleanup;

    hr = pSessionMgr->lpVtbl->GetSessionEnumerator(pSessionMgr, &pSessionEnum);
    if (FAILED(hr)) goto cleanup;

    hr = pSessionEnum->lpVtbl->GetCount(pSessionEnum, &count);
    if (FAILED(hr)) goto cleanup;

    for (int i = 0; i < count; i++) {
        IAudioSessionControl *pCtrl = NULL;
        IAudioSessionControl2 *pCtrl2 = NULL;
        ISimpleAudioVolume *pVolume = NULL;
        DWORD sessionPid = 0;

        hr = pSessionEnum->lpVtbl->GetSession(pSessionEnum, i, &pCtrl);
        if (FAILED(hr)) continue;

        hr = pCtrl->lpVtbl->QueryInterface(pCtrl, &IID_IAudioSessionControl2, (void **)&pCtrl2);
        if (FAILED(hr)) { pCtrl->lpVtbl->Release(pCtrl); continue; }

        hr = pCtrl2->lpVtbl->GetProcessId(pCtrl2, &sessionPid);
        if (SUCCEEDED(hr) && sessionPid == pid) {
            hr = pCtrl->lpVtbl->QueryInterface(pCtrl, &IID_ISimpleAudioVolume, (void **)&pVolume);
            if (SUCCEEDED(hr)) {
                BOOL m = FALSE;
                pVolume->lpVtbl->GetMute(pVolume, &m);
                muted = m;
                pVolume->lpVtbl->Release(pVolume);
            }
        }

        pCtrl2->lpVtbl->Release(pCtrl2);
        pCtrl->lpVtbl->Release(pCtrl);

        if (sessionPid == pid) break;
    }

cleanup:
    if (pSessionEnum) pSessionEnum->lpVtbl->Release(pSessionEnum);
    if (pSessionMgr) pSessionMgr->lpVtbl->Release(pSessionMgr);
    if (pDevice) pDevice->lpVtbl->Release(pDevice);
    if (pEnumerator) pEnumerator->lpVtbl->Release(pEnumerator);

    return muted;
}
