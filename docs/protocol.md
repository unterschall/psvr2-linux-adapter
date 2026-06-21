# PSVR2 USB Protocol (host side)

Byte layouts and control semantics used by the kernel module. Sourced from the
Monado PSVR2 driver and cross-checked against Sony's SIE OSS headers; see
[references.md](references.md). All multi-byte fields are little-endian.

## ep0 vendor control

Commands are issued as USB control transfers on endpoint 0:

| Direction | bmRequestType                                | bRequest | wValue     | wIndex |
|-----------|----------------------------------------------|----------|------------|--------|
| Set       | `VENDOR \| RECIP_ENDPOINT` (OUT)              | `0x09`   | report_id  | 0      |
| Get       | `VENDOR \| RECIP_ENDPOINT \| DIR_IN`          | `0x01`   | report_id  | 0      |

The payload is a `sie_ctrl_pkt`:

```c
struct sie_ctrl_pkt {
    __le16 report_id;
    __le16 subcmd;
    __le32 len;
    __u8   data[512 - 8];
} __packed;          /* transfer length = len + 8 */
```

Known report IDs:

| report_id | subcmd | meaning                         | payload                |
|-----------|--------|---------------------------------|------------------------|
| `0x12`    | 1      | Set panel brightness            | 1 byte, 0..31          |
| `0x08`    | 1      | Set peripheral / motor (haptic) | **not yet reversed**   |
| `0x0b`    | 1      | Set camera mode                 | `__le32 data[2]`       |
| `0x0c`    | —      | Enable/disable gaze stream      | subcmd 1=on, 2=off     |

No host authentication handshake is required to stream data. (Sony's
`sieusb.h` documents a device-side challenge/response, but it is not exercised
by the host.)

## IF7: status header + IMU records

Each interrupt transfer begins with one header, followed by an array of IMU
records filling the remainder of the transfer.

```c
struct psvr2_status_record_hdr {     /* 32 bytes */
    __u8 dprx_status;       /* 0 = DP link not ready */
    __u8 prox_sensor_flag;  /* 1 = headset worn */
    __u8 function_button;   /* 1 = pressed */
    __u8 empty0[2];
    __u8 ipd_dial_mm;       /* 59..72 mm */
    __u8 remainder[26];
} __packed;

struct psvr2_imu_record {            /* 24 bytes */
    __le32 vts_us;          /* video timestamp (us) */
    __le16 accel[3];
    __le16 gyro[3];
    __le16 dp_frame_cnt;
    __le16 dp_line_cnt;
    __le16 imu_ts_us;       /* IMU timestamp (us) */
    __le16 status;          /* bit0 = invalid sample */
} __packed;
```

Invalid samples are signalled either by `status & 1` or by the sentinel value
`0x8000` in a field. The module skips these.

### IMU scaling

Raw `__s16` register values are exposed verbatim on the IIO channels; the
physical conversion lives in the IIO `*_scale` attributes:

| Sensor        | SI unit | Scale (per LSB)                  |
|---------------|---------|----------------------------------|
| Accelerometer | m/s²    | `4 · 9.80665 / 32767` ≈ 1.197e-3 |
| Gyroscope     | rad/s   | `(2000 · π/180) / 32767` ≈ 1.065e-3 |

Axes are reported in the sensor's native order (`accel[0..2]`, `gyro[0..2]`).
Coordinate-frame remapping (e.g. Monado's convention) is intentionally left to
userspace.

## IF3: SLAM 6DoF pose

The headset's onboard tracker streams one 512-byte record per bulk transfer
(alt 0, endpoint `0x83`):

```c
struct psvr2_slam_record {           /* 512 bytes */
    char   sla_hdr[3];      /* "SLA" */
    __u8   const1;          /* observed 0x01 */
    __le32 pkt_size;        /* 0x200 */
    __le32 vts_ts_us;       /* device timestamp (us) */
    __le32 unknown1;        /* observed 3 */
    __le32 pos[3];          /* float32 position, metres */
    __le32 orient[4];       /* float32 quaternion, [0] = w */
    __u8   remainder[468];
} __packed;
```

The module validates the `"SLA"` magic, copies the float bit patterns through
untouched (no FPU use in kernel) and queues a `struct psvr2_pose_sample` on the
character device **`/dev/psvr2-pose`** (see `kernel/psvr2_uapi.h`). `read()`
returns whole samples; `poll()` reports `POLLIN` when data is available. The
latest raw record is also at `…/debugfs/psvr2/raw_slam`.

### Coordinate convention

Position and orientation are reported in the device's **native wire order**.
Monado remaps them as follows (apply in userspace if you want that convention):

| Output | From wire        |
|--------|------------------|
| pos.x  | `pos[2]`         |
| pos.y  | `pos[1]`         |
| pos.z  | `-pos[0]`        |
| quat.w | `orient[0]`      |
| quat.x | `-orient[2]`     |
| quat.y | `-orient[1]`     |
| quat.z | `orient[3]`      |

`unknown1` is normally `3`; other values may indicate a non-tracking record.

## IF6: cameras

Bulk IN endpoint `0x87`, alt 0. The headset delivers **one frame per bulk
transfer**; the transfer length identifies the camera mode, which is selected
beforehand via control report `0x0b` (`data[0] = 1`, `data[1] = mode`). Each
frame begins with a 256-byte header.

The headset exposes ~17 modes, many of which interleave several camera views in
exotic 8-bytes-per-pixel packings. The module currently decodes only the
simplest:

| Mode | Transfer size | Decoded as                         |
|------|---------------|------------------------------------|
| `0x1` (BOTTOM_SBS_CROPPED) | 819456 | `1280x640` 8-bit greyscale (two 640x640 bottom-camera views side by side), after the 256-byte header |

Mode 1 is exposed as a standard **V4L2 capture device** (`/dev/videoN`,
`V4L2_PIX_FMT_GREY`, 1280x640). Streaming on switches the headset into mode 1;
streaming off switches the cameras back off (mode 0). Other modes (interleaved
fisheye/controller-tracking views, BC4-compressed `0x10`, etc.) are documented
in Monado's `psvr2_protocol.h` (see [references.md](references.md)) and remain
future work.

## IF5: eye / gaze tracking

Bulk IN endpoint `0x85`, alt 0. The stream is **keepalive-gated**: the host
must send control report `0x0c` subcmd `0x01` to enable it, and re-send it about
once a second or the headset stops streaming (subcmd `0x02` disables). The
module runs a delayed work item to do this automatically while IF5 is bound.

Each packet is a `struct psvr2_pkt_gaze_state` beginning with ASCII `"GS"`,
containing per-eye and combined gaze data. Many fields are not yet understood;
the module surfaces the well-known ones in a curated
`struct psvr2_gaze_sample` (see `kernel/psvr2_uapi.h`) on the **`/dev/psvr2-gaze`**
character device, with the latest raw packet at `…/debugfs/psvr2/raw_gaze`.

| Field (per eye)     | Meaning                                  |
|---------------------|------------------------------------------|
| `gaze_point_mm`     | gaze origin, mm (float, device frame)    |
| `gaze_direction`    | gaze direction (float, unnormalised)     |
| `pupil_diameter_mm` | pupil diameter, mm (float)               |
| `blink`             | 0 = open, 1 = closed                     |

The combined entry adds a fused gaze point + normalised direction and the device
sample timestamp. As with pose, vectors are in the device's native frame
(Monado negates x and z); floats are carried as raw little-endian bit patterns.
