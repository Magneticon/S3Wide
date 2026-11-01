# ABI and raster contract notes

These notes record the public interface facts used to author the current
Win16 subset. No source from an unlicensed display-driver sample was copied.

* [GDIINFO](https://dos-help.soulsphere.org/ddag31qh.hlp/GDIINFO.html) gives
  the Windows 3.1 structure fields, member types, and capability semantics.
  The independent declaration in `include/s3_abi.h` is 55 16-bit words
  (110 bytes); the host build checks its total size and important offsets.
* [Enabling and Disabling the Physical Device](https://dos-help.soulsphere.org/ddag31qh.hlp/dispenable.html)
  describes the two `Enable` calls: first return GDIINFO, then initialize the
  allocated PDEVICE and hardware. The current source handles the first and
  deliberately fails the second until mode setup exists.
* [PDEVICE](https://dos-help.soulsphere.org/ddag31qh.hlp/PDEVICE.html) requires
  a nonzero first 16-bit member for a display device and leaves later fields
  driver-specific. The Win16 PDEV is 32 bytes, including a far pointer to the
  optional attached software surface.
* [ColorInfo](https://dos-help.soulsphere.org/ddag31qh.hlp/ColorInfo.html) gives
  ordinal 2, logical/physical conversion directions, and indexed-color
  passthrough. [Physical Colors](https://dos-help.soulsphere.org/ddag31qh.hlp/dispcolors.html)
  specifies the 0xFF high-byte marker for physical palette indexes. This
  milestone uses a fixed 3:3:2 index mapping in code; it does not program an
  S3 DAC.
* [GetDriverResourceID](https://dos-help.soulsphere.org/ddag31qh.hlp/GetDriverResourceID.html)
  gives ordinal 450 and requires the returned ID to remain associated with the
  requested resource type. The current implementation does not remap IDs.
* [BitBlt](https://dos-help.soulsphere.org/ddag31qh.hlp/BitBlt.html) documents
  ordinal 1, the `PDEVICE`/`PBITMAP` inputs, ROP3 contract, and that the
  `RC_BITBLT` capability controls use. The [PBITMAP](https://dos-help.soulsphere.org/ddag31qh.hlp/PBITMAP.html)
  page gives its full 32-byte layout, requires even `bmWidthBytes`, stores the
  top raster line first, and documents the `bmSegmentIndex`, `bmScanSegment`,
  and `bmFillBytes` fields used when bitmap storage spans segments. This
  adapter checks `bmWidthPlanes`, accepts only zero segment metadata and spans
  no larger than one segment, and implements 8-bpp top-down `PBITMAP` to
  display `SRCCOPY`; segmented sources and other formats/ROP3 values fail.
  `dpRaster` remains zero, so the operation is not advertised.
* [Output](https://dos-help.soulsphere.org/ddag31qh.hlp/Output.html) documents
  ordinal 8 and the three outcomes: 1 for success, 0 for failure, and -1 for an
  unsupported style so GDI can simulate it. `Output(OS_POLYLINE)` uses style
  18, requires at least two points, and draws each segment up to but excluding
  its endpoint; see the [polyline style contract](https://dos-help.soulsphere.org/ddag31qh.hlp/Output%28OS_POLYLINE%29.html).
  The adapter supports only 8-bpp `R2_COPYPEN` segments on an attached PDEV
  surface, clips to framebuffer bounds, and ignores `lpClipRect` because
  `dpClip` is zero; the driver enforces surface bounds itself.
  Unsupported styles/ROP2 values return the simulation result.
* [RealizeObject](https://dos-help.soulsphere.org/ddag31qh.hlp/RealizeObject.html)
  documents ordinal 10, required for graphics drivers, and its two-call
  physical-object sizing/fill contract. The Win16 `LOGPEN` input is 10 bytes
  (`UINT` style, 4-byte `POINT`, 32-bit `COLORREF`); see the [Windows 3.1 SDK
  header](https://gist.github.com/delfigamer/9223fa02f3ddf413e18c3a7447d083df)
  and [Windows 3.1 SDK reference](https://www.pcjs.org/software/pcx86/sdk/windows/3.10/).
  Field offsets and size are asserted. Physical pen formats are driver-owned;
  this driver defines a
  checked 12-byte PPEN containing style, width, and a fixed-palette physical
  color. Styled/wide logical pens are realized as
  nominal solid one-pixel pens for GDI-side simulation; explicit null pens
  stay null and negative logical widths are declined.
  Brushes, fonts, and device bitmaps are declined.
* [Pixel](https://dos-help.soulsphere.org/ddag31qh.hlp/Pixel.html) documents
  ordinal 9, the get-versus-set rule, and `80000000h` as its error return.
  [DRAWMODE](https://dos-help.soulsphere.org/ddag31qh.hlp/DRAWMODE.html)
  documents `Rop2` at offset zero and `R2_COPYPEN` as value 13. The adapter
  supports null-draw-mode reads and copy-pen writes; other modes fail.
* The official [Open Watcom C/C++ Programmer's Guide](https://open-watcom.github.io/open-watcom-v2-wikidocs/cpguide.html)
  says 16-bit far pointers address data within a 64-KiB segment, while larger
  data objects require huge-pointer arithmetic. The DDA `PBITMAP.bmBits` member
  is a far pointer. Its trailing segment fields are the documented mechanism
  for larger DDBs; this `BitBlt` adapter deliberately declines those and checks
  `FP_OFF(bmBits) + span` for the smaller one-segment case. A 1280x720 image
  needs 921,600 bytes and is rejected by this `BitBlt` path.
* The DDA [SetDIBitsToDevice](https://dos-help.soulsphere.org/ddag31qh.hlp/SetDIBitsToDevice.html)
  contract gives ordinal 21 and the ABI
  `SetDIBitsToDevice(PDEVICE, X, Y, iScan, cScans, clip, drawMode, bits,
  bitmapInfo, translate)`. `X,Y` name the destination top-left, `iScan` is the
  starting scan in the DIB buffer, `cScans` is the number copied, `lpDIBits`
  points at its byte data, and `lpTranslate` maps palette colors to physical
  device colors. This implementation accepts only a complete 256-entry table
  and translation array for 8-bpp indexed `BI_RGB`; the translation results
  must be physical indices 0..255. It rejects RLE and other bit depths.
* The DDA [BITMAPINFO](https://dos-help.soulsphere.org/ddag31qh.hlp/BITMAPINFO.html)
  and [BITMAPINFOHEADER](https://dos-help.soulsphere.org/ddag31qh.hlp/BITMAPINFOHEADER.html)
  pages describe the 40-byte header plus `RGBQUAD` color table, 8-bpp one-byte
  palette indexes, and scanline padding to 32-bit boundaries. The bitmap origin
  is lower-left, and segment boundaries can occur anywhere in the bitmap.
  Consequently the band adapter computes `stride=(width+3)&~3`, treats
  `iScan` as the first bottom-up scan in the supplied band, and places scan
  `iScan+row` at device row `Y+height-1-(iScan+row)`. It checks that the header
  plus the full palette, translation table, and each individual band fit in
  their respective 64-KiB far-pointer segment. The 1280x720 host reference
  uploads 32-line, 40,960-byte bands from offset 0x6000 (exactly ending at
  0x10000), verifies all 921,600 destination bytes, palette conversion, and
  all 14 framebuffer banks. A band crossing its source segment is rejected.
  The runtime DDI still is not advertised (`dpRaster` is zero) and the
  `EnableDevice` path still fails closed.

`S3_SurfaceBlitDIB8` is an independent host-testable software core using an
explicit byte length, stride, and row orientation. The DDA adapters only call
it when a PDEV has an attached surface. `Enable` currently leaves that pointer
null and still fails closed; no drawing capability is advertised. The local
`BITMAP`/`DRAWMODE` declarations are independently re-declared from the public
field descriptions and statically checked for Win16 layout. No DDK sample code
was copied.

The DDA source for `BitBlt` is a `PBITMAP`, not a `BITMAPINFO` DIB. Windows 3.x
DDA docs distinguish the 32-byte physical `PBITMAP` (top raster line first,
16-bit row alignment, explicit multi-segment metadata) from `BITMAPINFO` DIB
data (lower-left origin, 32-bit row padding, color table, segment boundaries
possible anywhere). The two adapters remain distinct rather than treating
their layouts as interchangeable.

The historical DDA pages are documentation references, not bundled source
code. The only licensed implementation dependency is Open Watcom V2, whose
license text and package hash are retained in `toolchain/`.
