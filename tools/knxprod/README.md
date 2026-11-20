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

Output: `build/knxprod/h8i8o.knxprod.xml`, application `M-00FA_A-0001-02` with 24 objects
and 48 parameters on 16 pages.

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

The exporter writes the table sizes from the build's KNstaX configuration. This project uses
the `host-minimal` profile, which has the same capacities as the firmware's `tp1-small`
(128 group addresses, 160 associations, APDU length 15); keep the two in step.
