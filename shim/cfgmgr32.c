/*
 * Minimal cfgmgr32.dll replacement for running Cortex Control under Wine 10.
 *
 * Wine 10 forwards CM_Get_Device_Interface_List_SizeW to a setupapi stub that
 * always fails, so hidapi (used by Cortex Control) never sees any HID device.
 * This implements the device-interface functions hidapi needs on top of
 * SetupDi*, which Wine does implement, and forwards the rest to setupapi
 * (exactly what Wine's own cfgmgr32 does for them).
 *
 * Set CFGSHIM_LOG=1 to trace calls to stderr.
 */
#include <windows.h>
#include <setupapi.h>
#include <devpropdef.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <stdlib.h>

static const DEVPROPKEY key_instance_id = {
    { 0x78c34fc8, 0x104a, 0x4aca, { 0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57 } }, 256
};

static int log_on = -1;
static void trace(const char *fmt, ...)
{
    va_list ap;
    if (log_on < 0) log_on = getenv("CFGSHIM_LOG") != NULL;
    if (!log_on) return;
    va_start(ap, fmt);
    fprintf(stderr, "cfgshim: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

/* Builds the REG_MULTI_SZ list of interface paths. Caller frees. */
static WCHAR *build_list(const GUID *guid, const WCHAR *device_id, ULONG flags, ULONG *len_out)
{
    DWORD digcf = DIGCF_DEVICEINTERFACE | ((flags & CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES) ? 0 : DIGCF_PRESENT);
    HDEVINFO set = SetupDiGetClassDevsW(guid, device_id, NULL, digcf);
    SP_DEVICE_INTERFACE_DATA iface = { sizeof(iface) };
    WCHAR *list = NULL;
    ULONG len = 0, cap = 0;
    DWORD i;

    if (set != INVALID_HANDLE_VALUE)
    {
        for (i = 0; SetupDiEnumDeviceInterfaces(set, NULL, guid, i, &iface); i++)
        {
            DWORD size = 0;
            SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail;
            ULONG n;

            SetupDiGetDeviceInterfaceDetailW(set, &iface, NULL, 0, &size, NULL);
            if (!size || !(detail = malloc(size))) continue;
            detail->cbSize = sizeof(*detail);
            if (SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, size, NULL, NULL))
            {
                n = wcslen(detail->DevicePath) + 1;
                if (len + n + 1 > cap)
                {
                    cap = (len + n + 1) * 2;
                    list = realloc(list, cap * sizeof(WCHAR));
                }
                memcpy(list + len, detail->DevicePath, n * sizeof(WCHAR));
                len += n;
                trace("  %ls", detail->DevicePath);
            }
            free(detail);
        }
        SetupDiDestroyDeviceInfoList(set);
    }

    if (!list) list = malloc(sizeof(WCHAR));
    list[len++] = 0;
    *len_out = len;
    return list;
}

CONFIGRET WINAPI CM_Get_Device_Interface_List_SizeW(PULONG len, LPGUID guid, DEVINSTID_W device_id, ULONG flags)
{
    WCHAR *list;
    if (!len || !guid) return CR_INVALID_POINTER;
    list = build_list(guid, device_id, flags, len);
    free(list);
    trace("List_SizeW -> %lu", *len);
    return CR_SUCCESS;
}

CONFIGRET WINAPI CM_Get_Device_Interface_List_Size_ExW(PULONG len, LPGUID guid, DEVINSTID_W device_id, ULONG flags, HMACHINE machine)
{
    return CM_Get_Device_Interface_List_SizeW(len, guid, device_id, flags);
}

CONFIGRET WINAPI CM_Get_Device_Interface_ListW(LPGUID guid, DEVINSTID_W device_id, PZZWSTR buffer, ULONG buffer_len, ULONG flags)
{
    ULONG len;
    WCHAR *list;
    if (!guid || !buffer) return CR_INVALID_POINTER;
    list = build_list(guid, device_id, flags, &len);
    if (len > buffer_len)
    {
        free(list);
        trace("ListW -> buffer too small (%lu > %lu)", len, buffer_len);
        return CR_BUFFER_SMALL;
    }
    memcpy(buffer, list, len * sizeof(WCHAR));
    free(list);
    trace("ListW -> %lu chars", len);
    return CR_SUCCESS;
}

CONFIGRET WINAPI CM_Get_Device_Interface_List_ExW(LPGUID guid, DEVINSTID_W device_id, PZZWSTR buffer, ULONG buffer_len, ULONG flags, HMACHINE machine)
{
    return CM_Get_Device_Interface_ListW(guid, device_id, buffer, buffer_len, flags);
}

static BOOL get_iface_device(HDEVINFO set, SP_DEVICE_INTERFACE_DATA *iface, SP_DEVINFO_DATA *dev)
{
    DWORD size = 0;
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail;
    BOOL ok;

    SetupDiGetDeviceInterfaceDetailW(set, iface, NULL, 0, &size, NULL);
    if (!size || !(detail = malloc(size))) return FALSE;
    detail->cbSize = sizeof(*detail);
    ok = SetupDiGetDeviceInterfaceDetailW(set, iface, detail, size, NULL, dev);
    free(detail);
    return ok;
}

static BOOL find_iface(LPCWSTR path, HDEVINFO *set_out, SP_DEVICE_INTERFACE_DATA *iface)
{
    const WCHAR *brace = wcsrchr(path, '{');
    unsigned int d[11];
    GUID guid;
    HDEVINFO set;
    DWORD i, size;
    int k;

    if (!brace || swscanf(brace, L"{%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}", &d[0], &d[1], &d[2],
                          &d[3], &d[4], &d[5], &d[6], &d[7], &d[8], &d[9], &d[10]) != 11)
        return FALSE;
    guid.Data1 = d[0]; guid.Data2 = d[1]; guid.Data3 = d[2];
    for (k = 0; k < 8; k++) guid.Data4[k] = d[3 + k];

    set = SetupDiGetClassDevsW(&guid, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return FALSE;
    for (i = 0; SetupDiEnumDeviceInterfaces(set, NULL, &guid, i, iface); i++)
    {
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail;
        BOOL match;

        size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, iface, NULL, 0, &size, NULL);
        if (!size || !(detail = malloc(size))) continue;
        detail->cbSize = sizeof(*detail);
        match = SetupDiGetDeviceInterfaceDetailW(set, iface, detail, size, NULL, NULL)
                && !_wcsicmp(detail->DevicePath, path);
        free(detail);
        if (match)
        {
            *set_out = set;
            return TRUE;
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return FALSE;
}

/* hidapi only asks for the interface's device instance ID. */
CONFIGRET WINAPI CM_Get_Device_Interface_PropertyW(LPCWSTR path, const DEVPROPKEY *key, DEVPROPTYPE *type,
                                                   PBYTE buffer, PULONG size, ULONG flags)
{
    HDEVINFO set;
    SP_DEVICE_INTERFACE_DATA iface = { sizeof(iface) };
    SP_DEVINFO_DATA dev = { sizeof(dev) };
    WCHAR id[MAX_DEVICE_ID_LEN];
    DWORD needed;
    CONFIGRET ret = CR_NO_SUCH_DEVICE_INTERFACE;

    if (!path || !key || !type || !size) return CR_INVALID_POINTER;
    if (memcmp(key, &key_instance_id, sizeof(*key)))
    {
        trace("Interface_PropertyW: unsupported key {%08lx}/%lu", key->fmtid.Data1, key->pid);
        return CR_NO_SUCH_VALUE;
    }

    /* SetupDiOpenDeviceInterfaceW is a stub in Wine 10, so find the
     * interface by walking its class (GUID taken from the path's tail). */
    if (!find_iface(path, &set, &iface)) set = INVALID_HANDLE_VALUE;
    if (set != INVALID_HANDLE_VALUE
        && get_iface_device(set, &iface, &dev)
        && SetupDiGetDeviceInstanceIdW(set, &dev, id, MAX_DEVICE_ID_LEN, NULL))
    {
        needed = (wcslen(id) + 1) * sizeof(WCHAR);
        *type = DEVPROP_TYPE_STRING;
        if (!buffer || *size < needed) ret = CR_BUFFER_SMALL;
        else
        {
            memcpy(buffer, id, needed);
            ret = CR_SUCCESS;
        }
        *size = needed;
        trace("Interface_PropertyW(%ls) -> %ls (%lu)", path, id, ret);
    }
    else trace("Interface_PropertyW(%ls) -> not found (err %lu)", path, GetLastError());
    if (set != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(set);
    return ret;
}
