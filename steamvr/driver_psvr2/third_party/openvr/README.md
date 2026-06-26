# Vendored OpenVR SDK files

These files are vendored from Valve's **OpenVR SDK** (v2.15.6),
<https://github.com/ValveSoftware/openvr>, licensed **BSD-3-Clause** (see
[`LICENSE`](LICENSE)). They are copied unmodified so the SteamVR driver builds
from a clean checkout without the full SDK.

| File | Origin in the SDK | Used for |
|------|-------------------|----------|
| `openvr_driver.h` | `headers/openvr_driver.h` | the OpenVR driver API the driver targets |
| `driverlog.h` / `driverlog.cpp` | `samples/drivers/utils/driverlog/` | the sample logging helper (`DriverLog`) |

To update: copy the same files from a newer SDK checkout and bump the version
above. Only these files are needed — `openvr_driver.h` is self-contained (C++
standard library only) and `driverlog` depends only on it.
