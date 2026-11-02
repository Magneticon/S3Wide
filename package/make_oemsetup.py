"""Write package/OEMSETUP.INF: one Windows Setup display entry per mode."""
import os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
os.makedirs('package', exist_ok=True)

modes = [(640, 480), (800, 600), (1024, 768), (1280, 720), (1280, 800),
         (1280, 1024), (1366, 768), (1600, 900), (1600, 1200), (1920, 1080),
         (1920, 1200), (2560, 1440), (3840, 2160)]
depths = [(8, '256 colors'), (16, '65K colors'), (32, '16M colors')]

lines = []
lines.append('; ---------------------------------------------------------------')
lines.append('; S3Wide display driver for Windows 3.1 / Windows for Workgroups 3.11')
lines.append('; Install with Windows Setup: Options, Change System Settings,')
lines.append('; Display, "Other display (Requires disk from OEM)...", then the')
lines.append('; folder that holds this file and S3WIDE.DRV.')
lines.append(';')
lines.append('; Every entry uses the same driver; the mode goes to [s3wide.drv] in')
lines.append('; SYSTEM.INI (Width, Height, Depth). If the video BIOS lacks the mode,')
lines.append('; the driver starts in the largest one it can draw that is no larger.')
lines.append('; 1280x720 256 colors on an S3 Trio64 uses the S3 registers directly.')
lines.append(';')
lines.append('; Run S3PREP first: it makes S3WIDE.DRV from S3WIDE.BIN and your own')
lines.append('; VGA.DRV, and copies the grabber, logo and fonts Setup installs for a')
lines.append('; VGA-style display (VGA.3GR, VGALOGO.*, VGA*.FON, *WOA.FON) into the')
lines.append('; package folder: when Setup asks for a Windows disk, give it that folder.')
lines.append('; ---------------------------------------------------------------')
lines.append('')
lines.append('[data]')
lines.append('    Version="3.10"')
lines.append('')
lines.append('[oemdisks]')
lines.append('    W =. ,"S3Wide display driver",s3widedisk')
lines.append('')
lines.append('[display]')
lines.append(';profile  = driver,       description,                        resolution,  286gr, logo code,     VDD,       386grabber, ega.sys, logo data,    work section')
work = []
# Profile and work-section names stay within 8 characters, like those in
# Windows' own SETUP.INF: sw<mode number><p|h|t> (256 / 65K / 16M colours).
for index, (w, h) in enumerate(modes):
    for d, colors in depths:
        key = 'sw%02d%s' % (index + 1, {8: 'p', 16: 'h', 32: 't'}[d])
        desc = '"S3Wide %4dx%-4d %s"' % (w, h, colors)
        lines.append('%-8s = W:s3wide.drv, %-36s, "100,96,96", , W:vgalogo.lgo, x:*vddvga, W:vga.3gr,,  W:vgalogo.rle, %s'
                     % (key, desc, key))
        work.append('')
        work.append('[%s]' % key)
        work.append(',,system.ini,s3wide.drv,"Width=","Width=%d"' % w)
        work.append(',,system.ini,s3wide.drv,"Height=","Height=%d"' % h)
        work.append(',,system.ini,s3wide.drv,"Depth=","Depth=%d"' % d)
text = '\r\n'.join(lines + work) + '\r\n'
open('package/OEMSETUP.INF', 'w', newline='').write(text)
print(len(modes) * len(depths), 'entries')
