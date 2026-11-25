# DropPoint setup manual

**Status: unverified — requires hardware.** The software in this manual is
built and tested in simulation and installed on an ARM64 Linux machine with
no aircraft attached. The hardware steps follow DJI's Payload SDK
documentation and each maker's published specification; none of them has
been carried out on an aircraft. Treat every step as a procedure to verify,
and do the bench test in chapter 11 before any flight.

## 1. How it fits together

DropPoint runs on a small Linux computer carried by the aircraft. Nothing is
installed on the remote controller: DJI Pilot 2 stays in control and shows
DropPoint's buttons and status text, which travel over the normal aircraft
radio link.

{{diagram:manual-overview|How the parts connect. Nothing plugs into the remote controller.}}

| Part | What it does |
|---|---|
| Aircraft | Supplies position, attitude, velocity and gimbal angles, and powers the computer |
| Companion computer | Runs DropPoint: works out where the payload will land and when to release |
| Drop kit | Holds and releases the payload |
| Remote controller with DJI Pilot 2 | Shows DropPoint's ten controls and its status window |

## 2. Choose your build

| | Build 1 — Matrice 4T | Build 2 — Matrice 400 with Manifold 3 | Build 3 — Matrice 400 with a generic computer |
|---|---|---|---|
| Computer | Raspberry Pi Zero 2 W (reference) | DJI Manifold 3 | Raspberry Pi or similar |
| Laser rangefinder for target height | No | Yes | No |
| Countdown sound and vibration on the RC | No | Yes | No |
| Target height comes from | Terrain tiles tied to the take-off point | Laser, with terrain as fallback | Terrain tiles tied to the take-off point |
| Software package | `droppoint-m4t_<version>_arm64.deb` | `.dpk` you make from `droppoint-m400_<version>_arm64.deb` | `droppoint-m400_<version>_arm64.deb` |
| Chapter | 5 | 6 | 7 |

Build 2 is the recommended Matrice 400 build. A Manifold 3 is not practical
on the Matrice 4T: it weighs 120 g of the aircraft's 200 g payload allowance.

## 3. Shopping list

Buy the drop kit from chapter 5 of the How-To guide ("Compatible drop kits").

### Build 1 — Matrice 4T

| Item | Qty | Notes |
|---|---|---|
| DJI E-Port Development Kit | 1 | Adapter board (about 27 g) and E-Port coaxial cable. Gives a 3.3 V UART, USB and 5 V / 12 V power |
| DJI SDK Certified Chip | 1 | Required on the Matrice 4T. DFN8 2 × 3 mm part: also buy a DFN8 2×3 socket adapter, or have it soldered to a small breakout board |
| Raspberry Pi Zero 2 W | 1 | About 11 g. Any ARM64 Linux computer with a UART, I2C and one free GPIO will do |
| microSD card, 16 GB or larger | 1 | For the computer's operating system |
| Jumper wires, 2.54 mm | about 10 | Keep them short; secure with tape or heat-shrink |
| Drop kit | 1 | JZ AD2 (43 g), CZI FT4-P (50 g) or CZI TH2 (70 g) |
| Kitchen scale, 1 g resolution | 1 | You must weigh the finished build |
| Windows PC with DJI Assistant 2 (Enterprise Series) | 1 | One-time binding |

### Build 2 — Matrice 400 with Manifold 3

| Item | Qty | Notes |
|---|---|---|
| Zenmuse H30 or H30T | 1 | Camera, gimbal and laser rangefinder |
| DJI Manifold 3 | 1 | 120 g. Mounts on the aircraft and plugs into E-Port V2 port E1, E2 or E3 (DJI recommends E3) |
| Drop kit | 1 | CZI TH4 V3, JZ PTS4, JZ UT4A, JZ PL10, DroMight Talon or Cybowindow 4-hook |
| USB-C cable | 1 | PC to the aircraft's USB-C debug port, to reach the Manifold 3 |
| Windows PC with DJI Assistant 2 (Enterprise Series) | 1 | One-time binding |

### Build 3 — Matrice 400 with a generic computer

| Item | Qty | Notes |
|---|---|---|
| Zenmuse H30 or H30T | 1 | Camera and gimbal |
| DJI E-Port V2 Development Kit with E-Port V2 coaxial cable | 1 | The certified chip is built into DJI's coaxial cable. The board gives USB and 5 V / 12 V power on XT30 connectors |
| USB-to-TTL serial module, FT232 or CP2102, 3.3 V | 1 | The E-Port V2 has no UART pins; this module provides one |
| Raspberry Pi (Zero 2 W, 4 or 5) or similar | 1 | ARM64 Linux, glibc 2.31 or newer |
| microSD card, XT30 pigtail, jumper wires | — | |
| Drop kit | 1 | As for Build 2 |

## 4. DJI developer account and credentials

1. Sign in at `developer.dji.com`, open the user centre and create an
   application. Choose type **Payload SDK** for Builds 1 and 3, and
   **Payload SDK – Manifold 3** for Build 2.
2. Wait for DJI to approve it, then note the five values DJI shows: app
   name, app ID, app key, app licence and your developer account email.
3. Keep them private. They go into one file, `credentials.yaml`, on the
   companion computer and nowhere else.

```
app_name: "..."
app_id: "..."
app_key: "..."
app_license: "..."
developer_account: "you@example.com"
baud_rate: "921600"
```

## 5. Build 1 — Matrice 4T

### 5.1 Prepare the Raspberry Pi

1. Flash **Raspberry Pi OS Lite (64-bit)** to the microSD card with Raspberry
   Pi Imager. In the imager's settings set a user name and password, your
   Wi-Fi, and enable SSH.
2. Boot the Pi, log in over SSH and edit `/boot/firmware/config.txt`. Add:

```
enable_uart=1
dtoverlay=disable-bt
dtparam=i2c_arm=on
```

3. Run `sudo raspi-config`, choose *Interface Options → Serial Port*, answer
   **No** to a login shell over serial and **Yes** to the serial hardware.
4. Reboot. Check that `/dev/serial0` and `/dev/i2c-1` exist.

`disable-bt` frees the Pi's full UART for the aircraft link; the default
mini-UART is not reliable at 921600 baud.

### 5.2 Wire the E-Port Development Kit to the Pi

Do all wiring with the aircraft **powered off**.

{{diagram:manual-m4t-wiring|Build 1 wiring. TX and RX cross over; all grounds are common.}}

| From | To Raspberry Pi | Pi header pin |
|---|---|---|
| E-Port kit UART **TX** | GPIO15 (RXD) | 10 |
| E-Port kit UART **RX** | GPIO14 (TXD) | 8 |
| E-Port kit **GND** | GND | 6 |
| E-Port kit **5 V** output | 5 V | 2 |

On the E-Port kit: set the USB mode switch to **Host** and the E-Port dip
switch to **ON**. Check the 5 V output with a multimeter before connecting
the Pi.

The E-Port coaxial USB-C cable is not keyed. If the kit does not power up,
one end is reversed: flip the other end to match.

### 5.3 Wire the SDK Certified Chip

The Matrice 4T will not accept a third-party payload without this chip.
DropPoint talks to it over I2C at address 0x2A and resets it at start-up.

| Certified chip pin | To Raspberry Pi | Pi header pin |
|---|---|---|
| VCC | 3.3 V | 1 |
| GND | GND | 9 |
| I2C_SDA | GPIO2 (SDA) | 3 |
| I2C_SCL | GPIO3 (SCL) | 5 |
| NRST | GPIO4 | 7 |

{{diagram:manual-pi-header|The nine Raspberry Pi header pins this build uses.}}

Check it responds before going further:

```
sudo -u droppoint /opt/droppoint/bin/droppoint --check-auth-chip --config /opt/droppoint/config
```

(Run this after installing the software in 5.5.) It should report the chip
found at 0x2A. If not, recheck the five wires and the chip's orientation in
its socket.

### 5.4 Mount and weigh

1. Fix the E-Port board and the Pi to the aircraft so that nothing can reach
   the propellers, block a vision sensor or cover the GNSS antenna.
2. Fit the drop kit as its maker instructs.
3. Weigh everything you added except the payload: computer, E-Port board,
   wiring, chip, mounts and drop kit. Write the figure down for 5.6.
4. The aircraft's limit is **200 g including the payload**.

### 5.5 Install the software

Copy the package to the Pi (for example with `scp`), then:

```
sudo dpkg -i droppoint-m4t_<version>_arm64.deb
```

{{shot:s6-m4t-install|Real capture: the same install on an ARM64 Linux machine.}}

### 5.6 Configure

1. Put your DJI values in the credentials file:

```
sudo nano /etc/droppoint/credentials.yaml
```

2. Edit the aircraft settings:

```
sudo nano /opt/droppoint/config/aircraft.yaml
```

| Setting | Set it to |
|---|---|
| `uart_device` | `/dev/serial0` |
| `onboard_equipment_kg` | The weight from 5.4, in kilograms |
| `release.kit` | Your kit: `jz_ad2`, `czi_ft4p` or `czi_th2` |
| `auth_chip.enabled` | `true` (already set in this profile) |

3. Check the configuration without the aircraft:

```
sudo -u droppoint /opt/droppoint/bin/droppoint --selftest --no-link --config /opt/droppoint/config
```

Every line should be `[OK]`, `[INFO]` or `[WARN]`. A `[FAIL]` line says what
to fix. Always run the self-test as the `droppoint` user, as shown.

### 5.7 Power up and bind

1. With everything connected, power on the aircraft and the remote
   controller. The Matrice 4T E-Port does not support hot-plugging: connect
   first, power second.
2. Connect the aircraft to the PC and open DJI Assistant 2. Sign in with the
   developer account, open the **Payload SDK** page and press **Bind**.
   Until it is bound, DropPoint's log says the SDK accessory is not bound.
3. Run the full self-test and read the report:

```
sudo systemctl stop droppoint
sudo -u droppoint /opt/droppoint/bin/droppoint --selftest --config /opt/droppoint/config
sudo systemctl start droppoint
```

Each telemetry topic should be listed as acquired. Record the result.

## 6. Build 2 — Matrice 400 with Manifold 3

{{diagram:manual-m400-manifold|Build 2. The Manifold 3, the camera and the drop kit each take one aircraft port.}}

### 6.1 Fit the hardware

1. Mount the Manifold 3 and plug it into E-Port V2 port **E3**. Secure the
   connector with its screws.
2. Fit the H30T on the gimbal port and the drop kit on its own port, as each
   maker instructs. Note which port the drop kit is on.
3. Weigh everything except the payload and note the figure.

### 6.2 Make the install file

A Manifold 3 application is a `.dpk` file that DJI Pilot 2 installs. DJI
checks the application ID inside it, so the file must contain your
credentials: you make it yourself and never share it.

1. Connect the PC to the aircraft's USB-C debug port and power the aircraft.
   The Manifold 3 answers at `192.168.42.140` on port E3 (`.120` on E1,
   `.130` on E2). DJI's default login is user `dji`, password `dji`.
2. Copy the package and your credentials file to it:

```
scp droppoint-m400_<version>_arm64.deb credentials.yaml dji@192.168.42.140:
```

3. Log in with `ssh dji@192.168.42.140`, unpack the package and edit the
   settings (`onboard_equipment_kg`, `release.kit`, `camera_mount_position`):

```
dpkg-deb -x droppoint-m400_*_arm64.deb dp
nano dp/opt/droppoint/config/aircraft.yaml
```

4. Make the install file:

```
dp/opt/droppoint/bin/droppoint-make-dpk --credentials credentials.yaml --out .
```

{{shot:s11-make-dpk|Real capture: making the install file on an ARM64 Linux machine with test credentials.}}

### 6.3 Install and start

1. Copy the `DropPoint-M400_v<version>.dpk` file to the remote controller by
   USB cable or SD card.
2. In DJI Pilot 2 open the Manifold 3 page, choose **Application
   Management**, press **+** and pick the file.
3. Open the camera view. DropPoint's icon appears on the left; tap it to
   start.
4. Bind the payload in DJI Assistant 2 as in 5.7 step 2.
5. Delete `credentials.yaml` and the `.dpk` from the Manifold 3 home folder
   and from the remote controller once it is installed.

For bench work you can instead install over SSH with
`dji_app_ctl install -i DropPoint-M400_v<version>.dpk`; DJI says to test the
final install through Pilot 2.

## 7. Build 3 — Matrice 400 with a generic computer

{{diagram:manual-m400-generic|Build 3. The USB-to-TTL module turns the E-Port V2's USB into a serial link.}}

1. Prepare the Raspberry Pi as in 5.1.
2. Connect the E-Port V2 coaxial cable from an aircraft E-Port V2 port to the
   development board. Set the board's USB toggle switch to the external USB
   port.
3. Plug the USB-to-TTL module into the board's USB port and wire its serial
   side to the Pi:

| USB-to-TTL module | To Raspberry Pi | Pi header pin |
|---|---|---|
| TXD | GPIO15 (RXD) | 10 |
| RXD | GPIO14 (TXD) | 8 |
| GND | GND | 6 |

4. Power the Pi from the board's 5 V XT30 output (pins 2 and 6 on the Pi).
   Measure it first.
5. Install with `sudo dpkg -i droppoint-m400_<version>_arm64.deb`, fill in
   `/etc/droppoint/credentials.yaml`, and set these in
   `/opt/droppoint/config/aircraft.yaml`:

| Setting | Set it to |
|---|---|
| `psdk_platform` | `generic_linux` |
| `advanced_features` | `false` |
| `psdk_link_mode` | `only_uart` |
| `uart_device` | `/dev/serial0` |
| `uart_usb_vid`, `uart_usb_pid` | FT232: `0x0403`, `0x6001`. CP2102: `0x10C4`, `0xEA60` |
| `rangefinder_present` | `false` |
| `onboard_equipment_kg`, `release.kit` | Your weight and kit |

6. Self-test, bind and full self-test as in 5.6 and 5.7.

## 8. Set up the drop kit

1. Set `release.kit` to the kit you bought. `droppoint --list-kits` prints
   the names.
2. Leave `release.backend` as `external_operator` to begin with: DropPoint
   guides and counts down, and the operator triggers the kit.
3. **CZI TH2 only:** in Pilot 2 set the bottom auxiliary light to **Off**,
   not Auto. The TH2 releases when that light comes on.
4. **Matrice 400 only, optional:** to let DropPoint trigger the kit itself,
   find the kit's drop button with

```
droppoint --probe-widgets <port number>
```

   then set `release.backend: psdk_widget` and fill in `release.widget` with
   the port and button number it printed. Bench test with empty hooks.

{{shot:s10-list-kits|Real capture: the kit list printed by the software.}}

## 9. Terrain data

Without the laser (Builds 1 and 3) target height comes from terrain tiles.

1. Get SRTM 1-arc-second `.hgt` tiles for your operating area. Each file is
   named after its south-west corner, for example `N33E073.hgt`.
2. Copy them to `/opt/droppoint/data/dem/` (Build 2: into `dp/opt/droppoint/data/dem/`
   before making the install file).
3. Always start DropPoint **before take-off**. On the ground it records the
   take-off height and ties the tiles to it.

With no tile for the area DropPoint assumes flat ground at take-off height
and says so on screen.

## 10. Controls in DJI Pilot 2

{{shot:s8-widgets-illustration|Illustration, not a screenshot: the ten controls DropPoint registers. DJI Pilot 2 cannot be captured without an aircraft.}}

SET TARGET, CONFIRM, ARM, RELEASE and ABORT appear on the main screen; CLEAR
TARGET, MODE, PAYLOAD, TEMP and SURFACE are on the payload settings page.

Map ARM, RELEASE and ABORT to remote-controller buttons in Pilot 2's custom
button settings. A physical button is safer than a touch target in flight.

## 11. Bench test — before any flight

Do this indoors or on the ground with **propellers removed** and **no
payload on the hooks**.

1. Power on. DropPoint's status window appears in Pilot 2 and reads
   `-- NO TARGET`.
2. Press each control once and confirm the status window reacts.
3. Check the attitude signs in the log: tilt the nose down and pitch should
   go negative; roll right and roll should go positive; point the camera
   straight down and gimbal pitch should read about −90°.
4. Press ABORT in every state and confirm it disarms.
5. Trigger the drop kit and confirm exactly one hook opens.
6. Power-cycle and confirm DropPoint starts by itself.

Stop and resolve anything that does not behave as described.

## 12. First flights and calibration

1. **Wind estimate.** Hover in a wind you have measured with a hand
   anemometer. Adjust `cda_airframe` in `aircraft.yaml` until DropPoint's
   wind matches, then set `airframe_calibrated: true`.
2. **Payload.** For each payload profile make five drops in calm air over an
   empty, marked area, measure where each lands, and run
   `droppoint_calibrate`. Update `payloads.yaml` with the result.
3. Keep people clear of the drop area at all times. Follow your aviation
   authority's rules for dropping objects from an aircraft.

## 13. Flying a drop

1. Start DropPoint before take-off and wait for its window in Pilot 2.
2. Climb to the release height and **hover for at least 5 seconds**.
3. Put the crosshair on the landing spot. SET TARGET, check the range and
   bearing shown, CONFIRM.
4. ARM. Follow the steering line, for example `>> MOVE 4.0m BRG 265`, until the
   window reads `>> DROP OK`.
5. Trigger the drop kit, then press RELEASE to log the drop.

{{shot:s9-floating-window|Text exactly as DropPoint sends it; shown here outside DJI Pilot 2.}}

## 14. Troubleshooting

| What you see | Likely cause | What to do |
|---|---|---|
| No DropPoint controls in Pilot 2 | Payload not bound, or certified chip not answering | Bind in DJI Assistant 2; run `--check-auth-chip` (Build 1) |
| Log says the SDK accessory is not bound | Binding not done | Chapter 5.7 step 2 |
| E-Port kit has no power | Coaxial USB-C cable reversed, or dip switch off | Flip one cable end; set the dip switch ON |
| Self-test: `required subscription topic <name> MISSING` | Link not up, wrong `uart_device`, TX and RX not crossed | Check wiring table and `uart_device` |
| Service will not start: aircraft model mismatch | Wrong package or profile for this aircraft | Install the package for your aircraft |
| Refuses to start: payload exceeds max_payload_kg | Build is too heavy for that payload | Lighten the build or remove that payload profile |
| `XX HOLD - NO WIND ESTIMATE, HOVER 5 S` | Not hovered long enough | Hold a steady hover for 5 seconds |
| `XX HOLD - NO TERRAIN ELEVATION AVAILABLE` | Started in the air, or no tile and flat fallback off | Land, restart on the ground; add tiles |
| `XX HOLD - GNSS SATELLITE COUNT LOW` | Fewer than 12 satellites | Wait in the open |
| AUTO cannot be selected | The drop kit is operator-triggered | Expected; use MANUAL |
| `.dpk` will not install | App ID or version mismatch, or example credentials | Remake it with your real credentials |

## 15. Upgrades, logs and removal

| Task | Command |
|---|---|
| Upgrade (keeps your settings and credentials) | `sudo dpkg -i droppoint-<aircraft>_<new version>_arm64.deb` |
| See the service state | `systemctl status droppoint` |
| Logs and the drop record | `/var/log/droppoint/` (`service.log`, `selftest.txt`, `drops.csv`) |
| Remove (keeps credentials and logs) | `sudo dpkg -r droppoint-<aircraft>` |

{{shot:s6-m4t-upgrade|Real capture: an upgrade keeps settings and credentials; removal keeps credentials and logs.}}
