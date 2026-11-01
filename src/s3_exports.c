#include "s3_abi.h"
#include "s3_device.h"

static S3_DEVICE_RUNTIME s3_display_runtime;

/* The DOSBox-X-tested candidate is not a Windows protected-mode backend.
   Refuse device enable until register I/O, aperture mapping, and rollback are
   connected to the DDA lifecycle and the scanout is established. */
int S3_FAR S3_PASCAL Enable(void S3_FAR *device, S3_I16 style,
                            char S3_FAR *device_type,
                            char S3_FAR *output_file,
                            char S3_FAR *extra_info)
{
    (void)device_type;
    (void)output_file;
    (void)extra_info;
    if (style & (S3_I16)S3_INQUIRE_INFO) {
        if (device == (void S3_FAR *)0 || !S3_BuildGDIInfo((S3_GDIINFO S3_FAR *)device)) {
            return 0;
        }
        return (int)sizeof(S3_GDIINFO);
    }
    /* Unknown style bits are unsupported; EnableDevice currently fails closed. */
    if (((S3_U16)style & (S3_U16)~S3_INFO_CONTEXT) != S3_ENABLE_DEVICE) {
        return 0;
    }
    return 0;
}

int S3_FAR S3_PASCAL GetDriverResourceID(int resource_id,
                                         char S3_FAR *resource_type)
{
    /* No resolution-specific resource remapping: preserve ID and type. */
    (void)resource_type;
    return resource_id;
}

int S3_FAR S3_PASCAL Disable(void S3_FAR *device)
{
    /* The ordinary Enable path remains fail-closed until a verified Windows
       mode/I-O provider is available. If a future injected provider enabled
       the device, keep bridge state intact when restore/free needs retry. */
    if (!s3_display_runtime.initialized) {
        S3_DeviceRuntimeInit(&s3_display_runtime);
    }
    return S3_DeviceDisableWithOps((S3_PDEV S3_FAR *)device,
                                   &s3_display_runtime);
}

int S3_FAR S3_PASCAL LibMain(unsigned short instance,
                             unsigned short data_segment,
                             unsigned short heap_size,
                             char S3_FAR *command_line)
{
    (void)instance;
    (void)data_segment;
    (void)heap_size;
    (void)command_line;
    S3_DeviceRuntimeInit(&s3_display_runtime);
    return 1;
}

int S3_FAR S3_PASCAL WEP(int reason)
{
    return reason;
}
