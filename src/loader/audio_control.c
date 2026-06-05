/*
 * audio_control.c - Process Audio Volume Control (WASAPI COM)
 *
 * Enumerates audio sessions to find the target process,
 * then uses ISimpleAudioVolume to control its volume.
 *
 * Instead of using SetMute (which can trigger audio session callbacks
 * in the game process and cause crashes), we set volume to 0.0 for mute
 * and restore the saved volume for unmute. This is the same approach
 * used by the Windows Volume Mixer.
 *
 * COM interfaces are accessed through vtable pointers for C compatibility.
 */

#include "audio_control.h"
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>

static BOOL s_com_initialized = FALSE;
static float s_saved_volume = 1.0f;

/* ─── Public API ──────────────────────────────────────────────────── */

BOOL AudioCtrl_Init(void)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr) || hr == S_FALSE) {
        s_com_initialized = TRUE;
        return TRUE;
    }
    if (hr == RPC_E_CHANGED_MODE) {
        s_com_initialized = FALSE;
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
                if (mute) {
                    /* Save current volume before muting */
                    pVolume->lpVtbl->GetMasterVolume(pVolume, &s_saved_volume);
                    if (s_saved_volume < 0.01f) s_saved_volume = 1.0f;
                    pVolume->lpVtbl->SetMasterVolume(pVolume, 0.0f, NULL);
                } else {
                    /* Restore saved volume */
                    pVolume->lpVtbl->SetMasterVolume(pVolume, s_saved_volume, NULL);
                }
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
                float vol = 1.0f;
                pVolume->lpVtbl->GetMasterVolume(pVolume, &vol);
                muted = (vol < 0.01f);
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
