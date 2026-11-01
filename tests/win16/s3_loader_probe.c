#include <windows.h>

typedef int (FAR PASCAL *S3_ENABLE_PROC)(void FAR *, short,
                                         char FAR *, char FAR *, char FAR *);
typedef int (FAR PASCAL *S3_RESOURCE_ID_PROC)(int, char FAR *);

static int show_result(LPCSTR message, int ok)
{
    MessageBox((HWND)0, message,
               ok ? "S3 loader probe: PASS" : "S3 loader probe: FAIL",
               MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONSTOP));
    return ok ? 0 : 1;
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

    module = LoadLibrary("C:\\S3TEST\\S3WIDE.DRV");
    if ((UINT)module <= HINSTANCE_ERROR) {
        return show_result("FAIL: LoadLibrary did not load S3WIDE.DRV.", 0);
    }

    enable_driver = (S3_ENABLE_PROC)GetProcAddress(module, (LPCSTR)5);
    resource_id = (S3_RESOURCE_ID_PROC)GetProcAddress(module, (LPCSTR)450);
    if (enable_driver == (S3_ENABLE_PROC)0 || resource_id == (S3_RESOURCE_ID_PROC)0) {
        FreeLibrary(module);
        return show_result("FAIL: expected DDA exports were not found.", 0);
    }

    for (i = 0; i < 55; ++i) info[i] = 0;
    result = enable_driver(info, 1, (char FAR *)0, (char FAR *)0,
                           (char FAR *)0);
    if (result != 110 || info[4] != 1280 || info[5] != 720 ||
        info[6] != 8 || info[7] != 1) {
        FreeLibrary(module);
        return show_result("FAIL: Enable(InquireInfo) returned unexpected GDIINFO.", 0);
    }

    if (resource_id(7, (char FAR *)0) != 7) {
        FreeLibrary(module);
        return show_result("FAIL: GetDriverResourceID changed the resource ID.", 0);
    }

    for (i = 0; i < sizeof(dummy_pdev); ++i) dummy_pdev[i] = 0;
    if (enable_driver(dummy_pdev, 0, (char FAR *)0, (char FAR *)0,
                      (char FAR *)0) != 0) {
        FreeLibrary(module);
        return show_result("FAIL: device Enable did not fail closed.", 0);
    }

    FreeLibrary(module);
    return show_result("PASS: exports loaded; GDIINFO is 1280x720x8; resource ID is preserved; device Enable returned 0.", 1);
}
