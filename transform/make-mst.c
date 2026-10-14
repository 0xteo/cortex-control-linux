/*
 * Generates cancel-dark.mst, a Windows Installer transform applied to the
 * Cortex Control installer with TRANSFORMS= at install time.
 *
 * Why: Wine draws "transparent" MSI text labels on the dialog's button-face
 * colour instead of on the background image. install.sh sets that colour to
 * black so the wizard's white text is readable on its dark background, which
 * leaves the Cancel confirmation (black text on a white image) unreadable.
 * This transform makes that one dialog match the rest of the wizard: it hides
 * its white background image and uses the wizard's white "Normal" text style.
 * Neural DSP's installer file and the installed app are not modified.
 *
 * Wine can't generate transforms (MsiDatabaseGenerateTransform is a stub), so
 * this writes the format directly, as read by Wine's msi/table.c.
 *
 * Build: x86_64-w64-mingw32-gcc -municode -o make-mst.exe make-mst.c -lole32
 * Run:   wine make-mst.exe cancel-dark.mst
 */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>

static const CLSID CLSID_MsiTransform =
    { 0x000c1082, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

/* String pool of the transform; references are 1-based indexes. */
static const char *strings[] = {
    "CancelDlg",                                                         /* 1 */
    "Text",                                                              /* 2 */
    "Background",                                                        /* 3 */
    "{\\Normal}Are you sure you want to cancel [ProductName] installation?", /* 4 */
};

/* Control table columns: Dialog_, Control (keys), Type, X, Y, Width, Height,
 * Attributes, Property, Text, Control_Next, Help. */
enum { COL_ATTRIBUTES = 7, COL_TEXT = 9 };
#define ATTR_VISIBLE 1
#define BACKGROUND_ATTRIBUTES 1048577 /* as shipped: visible | image */

static int utf2mime(int x)
{
    if (x >= '0' && x <= '9') return x - '0';
    if (x >= 'A' && x <= 'Z') return x - 'A' + 10;
    if (x >= 'a' && x <= 'z') return x - 'a' + 36;
    if (x == '.') return 62;
    if (x == '_') return 63;
    return -1;
}

/* Same as Wine's encode_streamname(TRUE, ...): MSI table stream names. */
static void encode_table_name(const char *in, WCHAR *out)
{
    *out++ = 0x4840;
    while (*in)
    {
        int ch = utf2mime(*in++), next;
        if (ch < 0) { *out++ = in[-1]; continue; }
        ch += 0x4800;
        if (*in && (next = utf2mime(*in)) >= 0)
        {
            ch += (next + 0x3ffffc0) << 6;
            in++;
        }
        *out++ = (WCHAR)ch;
    }
    *out = 0;
}

static HRESULT write_stream(IStorage *stg, const char *table, const void *data, ULONG size)
{
    WCHAR name[64];
    IStream *stm;
    HRESULT hr;

    encode_table_name(table, name);
    hr = IStorage_CreateStream(stg, name, STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &stm);
    if (FAILED(hr)) return hr;
    hr = IStream_Write(stm, data, size, NULL);
    IStream_Release(stm);
    return hr;
}

static void put16(BYTE **p, unsigned v) { *(*p)++ = v & 0xff; *(*p)++ = (v >> 8) & 0xff; }
static void put32(BYTE **p, unsigned v) { put16(p, v & 0xffff); put16(p, v >> 16); }

int wmain(int argc, WCHAR **argv)
{
    BYTE pool[64], data[256], rows[64], *p;
    unsigned i, n = sizeof(strings) / sizeof(strings[0]);
    IStorage *stg;
    HRESULT hr;

    if (argc != 2)
    {
        fprintf(stderr, "usage: make-mst.exe OUTPUT.mst\n");
        return 2;
    }

    /* _StringPool: codepage (1252, 2-byte refs), then (length, refcount) per
     * string; _StringData: the strings back to back. */
    p = pool;
    put32(&p, 1252);
    for (i = 0; i < n; i++) { put16(&p, strlen(strings[i])); put16(&p, 1); }
    ULONG pool_size = p - pool, data_size = 0;
    for (i = 0; i < n; i++)
    {
        memcpy(data + data_size, strings[i], strlen(strings[i]));
        data_size += strlen(strings[i]);
    }

    /* Control rows, in bitmask form: mask, the two key columns, then only
     * the columns whose bit is set. */
    p = rows;
    put16(&p, 1 << COL_TEXT);                      /* CancelDlg.Text: new style */
    put16(&p, 1); put16(&p, 2); put16(&p, 4);
    put16(&p, 1 << COL_ATTRIBUTES);                /* CancelDlg.Background: hidden */
    put16(&p, 1); put16(&p, 3);
    put32(&p, (BACKGROUND_ATTRIBUTES & ~ATTR_VISIBLE) ^ 0x80000000);
    ULONG rows_size = p - rows;

    CoInitialize(NULL);
    hr = StgCreateDocfile(argv[1], STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &stg);
    if (FAILED(hr)) { fprintf(stderr, "StgCreateDocfile failed %#lx\n", hr); return 1; }
    hr = IStorage_SetClass(stg, &CLSID_MsiTransform);
    if (SUCCEEDED(hr)) hr = write_stream(stg, "_StringPool", pool, pool_size);
    if (SUCCEEDED(hr)) hr = write_stream(stg, "_StringData", data, data_size);
    if (SUCCEEDED(hr)) hr = write_stream(stg, "Control", rows, rows_size);
    IStorage_Release(stg);
    if (FAILED(hr)) { fprintf(stderr, "writing transform failed %#lx\n", hr); return 1; }
    printf("wrote %ls\n", argv[1]);
    return 0;
}
