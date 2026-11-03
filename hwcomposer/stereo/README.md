# Private Android stereo layer input, version 1

This is a Waydroid experiment for Android 13, not a new public Android API.
A producer sets SurfaceControl metadata key `0x53544552` on its buffer layer, using a Parcel containing exactly one int32.
An empty Parcel withdraws the key.
Unknown values and malformed Parcels remain different from explicit mono.
Controls and container layers must not carry the key.
Metadata changes must accompany the corresponding buffer transaction.

| Value | Input |
|---|---|
| 0 | Unset |
| 1 | Explicit mono |
| 2, 3 | Side by side, left or right first |
| 4, 5 | Top and bottom, left or right first |
| 6, 7 | Alternating rows, left or right first |
| 8, 9 | Alternating columns, left or right first |
| 10, 11 | Checkerboard, upper-left sample left or right |
| 12, 13 | Temporal, left or right first |
| 14, 15 | AVC tile arrangement, frame 0 left or right |
| 16, 17 | Separate views, first view left or right |
| 18, 19 | Matroska cyan/red or green/magenta anaglyph |
| 20 | Unspecified relationship between constituent views |
| 21, 22 | Side-by-side quincunx, left or right first |

These values are private semantic identifiers, not copied container wire numbers.
Packing vocabulary follows AVC Annex D.2.26 and Matroska RFC 9559 StereoMode; AVC interpretation 0 remains unspecified.
AVC types 0/1/2/3/4/5/6/7 correspond to checkerboard/columns/rows/SBS/TB/temporal/mono/tiles.
RFC 9559 values 13/14 describe laced separate views; private values 16/17 preserve that vocabulary for diagnostics.
The producer assembles the decoded views into one left-first SBS buffer and uses value 2 for delivery.
Future extraction work must retain field-view, grid-position and flipping information, not infer these attributes from packing alone.

The current spatial copy implementation preserves samples and does no upconversion.
For AVC tiles, R0 supplies frame 0; R1 with R2 above R3 supplies frame 1; R4 is discarded.
See [ITU H.264 V15, Figure D-12](https://www.itu.int/rec/dologin_pub.asp?id=T-REC-H.264-202408-I%21%21PDF-E&lang=e&type=items) and [RFC 9559](https://www.rfc-editor.org/rfc/rfc9559.html).
Checkerboard rows are compacted without reconstructing absent samples.
SBS quincunx retains its sampled rows unchanged.

Temporal and separate-view inputs cannot be reconstructed from one buffer and an integer.
They produce a `WaydroidStereo` error naming the layout and requiring the producer to hand one side-by-side buffer, left first.
The producer copies both views at their own resolution into that buffer and marks it SBS left-first.
SurfaceFlinger and HWC do not pair layers or temporal arrivals; one buffer keeps both eyes in the same presentation.
Media decoding and its own packing belong to the later MediaFormat/Codec2 work.
Anaglyph cannot reconstruct two original full-colour images by sample copying and is also explicitly rejected.
Unknown or unspecified layouts, unreadable buffers, unsupported CPU formats and cropped/transformed packed input are diagnosed rather than falsely declared.
This restriction is visible and is not a claim of completed format support.

The Binder extension receives a stable HWC2 ID.
The adapter negotiates `WAYDROID_STEREO_IDENTITY_QUERY`, then calls the private device's `set_layer_ids` immediately before HWC1 prepare, in actual array order.
No Z-order assumption, public HWC1 ABI change or reserved-byte use is involved.
A missing/old extension advertises no stereo support.

Each Wayland child owns its declaration; moves transfer ownership, mono/unset destroy it before commit, and unused children clear before null-buffer commit.
Copied buffers use a separate event queue dispatched by the HWC thread and are never overwritten before wl_buffer.release.
Storage is bounded to three buffers per child.
The edition protocol XML is vendored unchanged from libstereo-declare, generated with wayland-scanner, and bound at version 2 when available.
Only side_by_side_full is declared, with wp_viewport destination describing one eye's logical size.
SBS-left buffers retain the existing import path without a stereo copy.
Modes without per-layer subsurfaces keep the composed framebuffer undeclared and report the missing isolation.

The legacy inactive-window snapshot copies one buffer without per-layer identity.
It explicitly clears existing stereo marks and logs a 2D snapshot fallback; live per-layer delivery can declare again.
Preserving stereo across that snapshot requires a per-child snapshot implementation and runtime proof.
Normal mono frames retain the existing 100 ms acquire-fence warning/commit behavior.
Stereo reads wait before import/copy and withdraw the child when that fence fails.
Dimensions are limited by checked protocol/SHM integer ranges and allocation success, not an arbitrary image raster limit.

The default gralloc route repacks the already imported, tightly packed BGRA SHM buffer.
That importer runs after the acquire fence, and its successful-copy state is reset before every update so a failed mapping cannot reuse old stereo pixels.
No second native-buffer lock, stride guess or red/blue swap is needed for this route.
The native GBM/Cros mapping checks remain in place for other buffer transports.
