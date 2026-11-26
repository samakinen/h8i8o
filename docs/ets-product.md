# ETS product file

`app/product.hpp` is the only description of the device. The firmware compiles it, and the
firmware build also turns it into the ETS product file, through KNstaX's `knx_ets_product()`
(KNstaX `docs/reference/ets_export_guide.md`). The product file is built on the host with the
firmware's own KNstaX configuration (`knx.defconfig` and the variant), so it declares what
the firmware implements.

## Build

The firmware build ([README](../README.md#build-the-firmware)) writes, next to `h8i8o.elf`:

| File | What it is |
|---|---|
| `h8i8o_<variant>.knxprod.xml` | The product as KNX project XML, validated against the KNX schema |
| `h8i8o_<variant>.ets.xml` | The same without `MasterData`: what the packer signs |
| `h8i8o_<variant>.json` | The product as the firmware sees it; the file to diff |
| `dist/h8i8o_<variant>/` | The above with `h8i8o.elf`, `h8i8o.bin` and a `manifest.json` naming the commits |

The variants:

| Variant | Application | Objects | Parameters | Data Secure |
|---|---|---:|---:|---|
| `standard` | `M-00FA_A-0001-05`, "H8i8o 8 inputs 8 outputs" | 74 | 283 on 17 pages | yes (64 group keys, 16 senders, 2 point-to-point keys) |
| `extended` | `M-00FA_A-0002-05`, "H8i8o 8 inputs 8 outputs, extended" | 90 | 347 on 25 pages | no |

Each object a channel's function uses is shown by ETS through an object view
(`kObjectViews` in `app/product.hpp`): one `ComObjectRef` per function, inside a `<choose>`
on the channel's function parameter, with the datapoint type of that function.

## Pack and sign

ETS imports a signed `.knxprod`. Signing needs the ETS libraries, so it runs on a Windows PC
with ETS6 and [OpenKNXproducer](https://github.com/OpenKNX/OpenKNXproducer) installed
(`OpenKNXproducer` on `PATH`, or its path in `OPENKNXPRODUCER`). From the repository root:

```
python external/KNstaX/tools/ets_product/ets_product.py pack build/fw/dist
```

This packs every product in the directory into `h8i8o_<variant>.knxprod` next to its
`.ets.xml`, checks the archive and records the packer in `manifest.json`. A build on that PC
outside Docker packs by itself. Kaenx-Creator also imports either XML file and exports a
`.knxprod`.

Then import the `.knxprod` into ETS6 and add "H8i8o 8 inputs 8 outputs" (or the extended one)
to a project. Application version 4 was tested with ETS6 and OpenKNXproducer 4.3.12; version
5, with its conditional objects, packs and signs with the same OpenKNXproducer but has not
been imported into ETS6 yet.

The product uses the example manufacturer ID 0x00FA, which ETS shows as "KNX Association".
The file declares the product as registered so that ETS imports it; it is not registered
with or certified by the KNX Association.

## When the product changes

ETS treats a file with new objects or parameters as a new application program, and refuses a
changed program under a version it has already imported. `product.lock` records the
application, version and content of each variant's product, and the build checks it:

- The product changed and `applicationVersion` in `app/product.hpp` did not: the build fails.
  Raise the version, rebuild, and commit `product.lock` with the change.
- The version was raised with the change: the build records it in `product.lock`.

Then re-import the product and change the device's application program in the ETS project.
`-DKNX_ETS_LOCK_MODE=warn` reports instead of failing, for iterations that never reach ETS.

A change to `knx.defconfig`, to a variant, or a KNstaX update that changes the exporter's
output changes the product as well, and the lock catches it the same way.
