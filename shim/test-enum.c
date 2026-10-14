/* Checks the shim: lists HID interfaces and resolves the Quad Cortex instance ID.
 * Run next to cfgmgr32.dll: WINEDLLOVERRIDES=cfgmgr32=n wine test-enum.exe */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
typedef struct { GUID fmtid; ULONG pid; } PK;
typedef DWORD (WINAPI *SZ)(PULONG, LPGUID, LPWSTR, ULONG);
typedef DWORD (WINAPI *LS)(LPGUID, LPWSTR, LPWSTR, ULONG, ULONG);
typedef DWORD (WINAPI *PR)(LPCWSTR, const PK *, ULONG *, BYTE *, PULONG, ULONG);
int main(void) {
    GUID hid = {0x4d1e55b2,0xf16f,0x11cf,{0x88,0xcb,0x00,0x11,0x11,0x00,0x00,0x30}};
    PK inst = {{0x78c34fc8,0x104a,0x4aca,{0x9e,0xa4,0x52,0x4d,0x52,0x99,0x6e,0x57}},256};
    HMODULE m = LoadLibraryW(L"cfgmgr32.dll");
    SZ sz = (SZ)GetProcAddress(m, "CM_Get_Device_Interface_List_SizeW");
    LS ls = (LS)GetProcAddress(m, "CM_Get_Device_Interface_ListW");
    PR pr = (PR)GetProcAddress(m, "CM_Get_Device_Interface_PropertyW");
    ULONG len = 0, t, n; WCHAR *buf, *p; BYTE out[512];
    if (!m || !sz || !ls || !pr) { printf("load failed\n"); return 1; }
    /* Wine marks HID devices present a second or two after it starts, so
     * retry like Cortex Control does instead of failing on a cold start. */
    for (int attempt = 1; attempt <= 10; attempt++, Sleep(1000)) {
        DWORD rc;
        if (sz(&len, &hid, NULL, 0) || !(buf = malloc(len * sizeof(WCHAR)))) continue;
        rc = ls(&hid, NULL, buf, len, 0);
        if (!rc) for (p = buf; *p; p += wcslen(p) + 1) if (wcsstr(p, L"vid_152a")) {
            n = sizeof(out); rc = pr(p, &inst, &t, out, &n, 0);
            printf("attempt %d: Quad Cortex found, instance id rc=%lu -> %ls\n", attempt, rc, rc ? L"(none)" : (WCHAR *)out);
            free(buf);
            return rc ? 1 : 0;
        }
        free(buf);
    }
    printf("no Quad Cortex found after 10s\n");
    return 1;
}
