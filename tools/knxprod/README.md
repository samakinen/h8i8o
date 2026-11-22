# ETS product file

`app/product.hpp` is the only description of the device. The firmware compiles it, and this
host project turns it into the product XML through KNstaX's exporter (KNstaX
`docs/reference/ets_export_guide.md`). The XML must then be packed and signed into a
`.knxprod` before ETS can import it.

## Build the XML

Host toolchain image, once:

```
docker build -t knstax-host -f tools/docker/Dockerfile.host tools/docker
```

Then, from the repository root (on Git Bash for Windows, prefix with `MSYS_NO_PATHCONV=1`):

```
docker run --rm -v "$PWD":/work -w /work knstax-host \
    sh -c "cmake -S tools/knxprod -B build/knxprod -G Ninja && cmake --build build/knxprod --target h8i8o_knxprod"
```

Output: `build/knxprod/h8i8o.knxprod.xml`, application `M-00FA_A-0001-04` with 24 objects
and 56 parameters on 16 pages, KNX Data Secure capable (64 group keys, 16 senders, 2
point-to-point keys).

## Pack and sign

Signing needs the ETS libraries, so it runs on a Windows PC with ETS6 installed. Either tool
works:

- **OpenKNXproducer.** Remove the `MasterData` element first (the export guide explains
  why), then pack:

  ```
  python tools/knxprod/strip_masterdata.py build/knxprod/h8i8o.knxprod.xml build/knxprod/h8i8o.ets.xml
  OpenKNXproducer knxprod -o build/knxprod/h8i8o.knxprod build/knxprod/h8i8o.ets.xml
  ```

- **Kaenx-Creator.** Import the XML and export a `.knxprod`.

Then import `h8i8o.knxprod` into ETS6 and add "H8i8o 8 inputs 8 outputs" to a project.
Tested with ETS6 and OpenKNXproducer 4.3.12.

The product uses the example manufacturer ID 0x00FA, which ETS shows as "KNX Association".
The file declares the product as registered so that ETS imports it; it is not registered
with or certified by the KNX Association.

## When the product changes

ETS treats a file with new objects or parameters as a new application program. Raise
`applicationVersion` in `app/product.hpp`, rebuild, re-import, and change the device's
application program in the ETS project.

The exporter writes the table sizes from the build's KNstaX configuration. This build uses
the `host-minimal` profile with the repository's `knx.defconfig` on top, the same file the
firmware applies to `tp1-small`, so the product file declares the firmware's capacities
(64 group addresses, 96 associations, the Data Secure key tables). A change to
`knx.defconfig` is a change of the product: raise `applicationVersion`. `knxprod.defconfig`
then turns off what only a device needs (KNXnet/IP, the virtual bus), so the build needs no
mbedTLS and also works outside Linux.
