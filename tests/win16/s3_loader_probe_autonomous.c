#include <windows.h>
#include <stdio.h>

typedef int (FAR PASCAL *S3_ENABLE_PROC)(void FAR *, short,
                                         char FAR *, char FAR *, char FAR *);
typedef int (FAR PASCAL *S3_RESOURCE_ID_PROC)(int, char FAR *);

static FILE *result_file;
static unsigned int failures;

static void record_check(const char *name, int passed, const char *detail)
{
    fprintf(result_file, "%s=%s: %s\n", name,
            passed ? "PASS" : "FAIL", detail);
    if (!passed) ++failures;
    fflush(result_file);
}

static void record_skip(const char *name, const char *detail)
{
    fprintf(result_file, "%s=SKIP: %s\n", name, detail);
    fflush(result_file);
}

static int finish_probe(void)
{
    int passed;

    passed = (failures == 0);
    fprintf(result_file, "FINAL=%s; failures=%u\n",
            passed ? "PASS" : "FAIL", failures);
    fflush(result_file);
    fclose(result_file);

    /* Shut down only the isolated guest so DOSBox-X's autoexec can exit. */
    if (!ExitWindows(0, 0)) return 2;
    return passed ? 0 : 1;
}

int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR command_line, int show_command)
{
    HINSTANCE module;
    S3_ENABLE_PROC enable_driver;
    S3_RESOURCE_ID_PROC resource_id;
    short info[55];
    char dummy_pdev[32];
    unsigned int i;
    int result;

    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show_command;

    result_file = fopen("D:\\OUTPUT\\RESULT.TXT", "w");
    if (result_file == (FILE FAR *)0) {
        ExitWindows(0, 0);
        return 2;
    }
    failures = 0;

    module = LoadLibrary("C:\\S3TEST\\S3WIDE.DRV");
    if ((UINT)module <= HINSTANCE_ERROR) {
        record_check("LoadLibrary", 0, "S3WIDE.DRV did not load");
        record_skip("GetProcAddress_5", "driver module unavailable");
        record_skip("GetProcAddress_450", "driver module unavailable");
        record_skip("Enable_InquireInfo", "driver module unavailable");
        record_skip("GetDriverResourceID", "driver module unavailable");
        record_skip("Enable_Device", "driver module unavailable");
        record_skip("FreeLibrary", "driver module unavailable");
        return finish_probe();
    }
    record_check("LoadLibrary", 1, "S3WIDE.DRV loaded");

    enable_driver = (S3_ENABLE_PROC)GetProcAddress(module, (LPCSTR)5);
    resource_id = (S3_RESOURCE_ID_PROC)GetProcAddress(module, (LPCSTR)450);
    record_check("GetProcAddress_5", enable_driver != (S3_ENABLE_PROC)0,
                 "DDA Enable ordinal 5");
    record_check("GetProcAddress_450", resource_id != (S3_RESOURCE_ID_PROC)0,
                 "GetDriverResourceID ordinal 450");

    if (enable_driver != (S3_ENABLE_PROC)0) {
        for (i = 0; i < 55; ++i) info[i] = 0;
        result = enable_driver(info, 1, (char FAR *)0, (char FAR *)0,
                               (char FAR *)0);
        record_check("Enable_InquireInfo",
                     result == 110 && info[4] == 1280 && info[5] == 720 &&
                     info[6] == 8 && info[7] == 1,
                     "expected 110-byte 1280x720x8, one-plane GDIINFO");
        /* GDIINFO dpMHiWin is at byte 56: short index 28 (x) and 29 (y). */
        record_check("Enable_MetricHiWin",
                     info[28] > 0 && info[29] > 0,
                     "metric high-resolution window extents are positive");

        for (i = 0; i < sizeof(dummy_pdev); ++i) dummy_pdev[i] = 0;
        result = enable_driver(dummy_pdev, 0, (char FAR *)0, (char FAR *)0,
                               (char FAR *)0);
        record_check("Enable_Device", result == 0,
                     "device Enable fails closed with return 0");
    } else {
        record_skip("Enable_InquireInfo", "ordinal 5 was unavailable");
        record_skip("Enable_Device", "ordinal 5 was unavailable");
    }

    if (resource_id != (S3_RESOURCE_ID_PROC)0) {
        record_check("GetDriverResourceID", resource_id(7, (char FAR *)0) == 7,
                     "requested resource ID is preserved");
    } else {
        record_skip("GetDriverResourceID", "ordinal 450 was unavailable");
    }

    FreeLibrary(module);
    record_check("FreeLibrary", 1, "unload call returned (API has no result)");
    return finish_probe();
}
