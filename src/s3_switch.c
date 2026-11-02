#include "s3_display.h"
#include "s3_trace.h"

/* Screen switching for full-screen DOS sessions in 386 Enhanced Mode.

   At Enable the driver tells the VGA virtual device that it saves and
   restores the screen itself (INT 2Fh AX=4000h) and hooks INT 2Fh. The
   virtual device then announces each switch to the Windows VM:
   AX=4001h when a DOS session takes the screen, AX=4002h when Windows
   gets it back. In between, drawing on the display is skipped (the
   hardware and its memory belong to the DOS session); on the way back the
   driver sets its mode again and asks USER (RepaintScreen) to redraw
   everything, or waits while USER has repainting disabled. */

static volatile S3_U16 s3_background;

#if defined(S3_HOST_TEST)
void S3_SwitchStart(void)
{
}

void S3_SwitchStop(void)
{
}

void S3_SwitchRepaintDisable(S3_U16 disable)
{
    (void)disable;
}
#else
#include <dos.h>
#include <i86.h>

typedef void (S3_FAR S3_PASCAL *S3_REPAINT_PROC)(void);
extern S3_U16 S3_FAR S3_PASCAL GetModuleHandle(const char S3_FAR *module_name);
extern S3_REPAINT_PROC S3_FAR S3_PASCAL GetProcAddress(S3_U16 module,
                                                       const char S3_FAR *proc_name);

/* USER's RepaintScreen export. */
#define S3_USER_REPAINTSCREEN 275

static void (__interrupt __far *s3_previous_int2f)(void);
static S3_U16 s3_hooked;
static S3_U16 s3_repaint_disabled, s3_repaint_pending;
static S3_REPAINT_PROC s3_repaint;

static void s3_switch(S3_U16 to_foreground)
{
    /* Only a real change counts: a repeated message would unbalance the
       software cursor's hold. */
    if (!to_foreground) {
        if (s3_background) return;
        s3_background = 1;
        S3_DisplaySuspend();
        return;
    }
    if (!s3_background) return;
    (void)S3_DisplayResume();
    s3_background = 0;
    if (s3_repaint_disabled) {
        s3_repaint_pending = 1;
    } else if (s3_repaint != (S3_REPAINT_PROC)0) {
        s3_repaint();
    }
}

static void __interrupt __far s3_int2f(union INTPACK registers)
{
    if (registers.w.ax == 0x4001 || registers.w.ax == 0x4002) {
        S3_PUSH32();
        s3_switch((S3_U16)(registers.w.ax == 0x4002));
        S3_POP32();
        registers.w.flags &= (unsigned short)~INTR_CF;
        return;
    }
    _chain_intr(s3_previous_int2f);
}

static void s3_int2f_call(S3_U16 function)
{
    __asm {
        mov ax, function
        int 2Fh
    }
}

void S3_SwitchStart(void)
{
    S3_U16 user;
    if (s3_hooked) return;
    if (s3_repaint == (S3_REPAINT_PROC)0) {
        user = GetModuleHandle("USER");
        if (user != 0) {
            s3_repaint = GetProcAddress(user, (const char S3_FAR *)MK_FP(0, S3_USER_REPAINTSCREEN));
        }
    }
    s3_background = 0;
    s3_repaint_pending = 0;
    s3_previous_int2f = _dos_getvect(0x2F);
    _dos_setvect(0x2F, s3_int2f);
    s3_hooked = 1;
    s3_int2f_call(0x4000);  /* the driver saves and restores the screen */
    S3_TRACE_EVENT("switch.start", s3_repaint != (S3_REPAINT_PROC)0, 0);
}

void S3_SwitchStop(void)
{
    if (!s3_hooked) return;
    s3_int2f_call(0x4007);  /* the virtual device takes the screen back */
    _dos_setvect(0x2F, s3_previous_int2f);
    s3_hooked = 0;
    s3_background = 0;
}

void S3_SwitchRepaintDisable(S3_U16 disable)
{
    s3_repaint_disabled = disable;
    if (!disable && s3_repaint_pending) {
        s3_repaint_pending = 0;
        if (s3_repaint != (S3_REPAINT_PROC)0) s3_repaint();
    }
}
#endif

int S3_SwitchInBackground(void)
{
    return s3_background != 0;
}

int S3_DisplayBlocked(const void S3_FAR *device, const void S3_FAR *other)
{
    if (!s3_background) return 0;
    return (device != (const void S3_FAR *)0 &&
            ((const S3_PDEV S3_FAR *)device)->pdType == S3_PDEV_DISPLAY) ||
           (other != (const void S3_FAR *)0 &&
            ((const S3_PDEV S3_FAR *)other)->pdType == S3_PDEV_DISPLAY);
}
