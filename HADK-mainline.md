# Sailfish OS Mainline HADK

Hardware adaptation guide for porting Sailfish OS to devices that run a mainline Linux kernel.

As of 2026-11-10. Written from the Moto X 2014 (victara) port: Sailfish OS 5.2.0.22, kernel 7.3.3, armv7hl.


## 1. Overview

The regular [HADK](https://hadk.sailfishos.org/) builds Sailfish OS on top of Android: an **Android base**, droid-hal and libhybris. A **mainline port** has none of that. The kernel's own drivers handle the hardware:

- graphics through Mesa
- audio through ALSA and PulseAudio
- sensors through IIO
- vibration through the kernel's ff-memless input interface

The rest is the same as in the HADK: the Platform SDK, `build_packages.sh`, mic and kickstarts, and mostly the same package names. Instead of an Android tree you keep five small repos.

How this guide is laid out:

- **Chapters 1-3**: what you need and which repos.
- **Chapters 4-12**: a walkthrough that creates the repos and builds the bare minimum, up to a flashable zip and a shell on the device.
- **Chapter 13**: the problems we hit on victara after that (charging detection, LED, audio, ...) and how we fixed them.
- **Chapter 14**: a checklist.

Conventions:

- The line above each command block names where it runs: **PLATFORM SDK**, **HOST** or **DEVICE**.
- `$ANDROID_ROOT`, `$VENDOR`, `$DEVICE`, `$PORT_ARCH` and `$RELEASE` come from `~/.hadk.env` (chapter 4).
- In file contents, replace `<device>`, `<vendor>` and the like with your own values.


## 2. Prerequisites

- **A mainline kernel that boots on your device**, with at least display, USB and storage working. postmarketOS makes a good test bench for bringing that up before Sailfish is involved.
- **The Sailfish OS Platform SDK.**
- **A way to flash and boot the device**: the stock bootloader, or a second-stage bootloader.
- **Your device's details**: the partition layout, how its bootloader wants the boot image (offsets, page size, appended DTB or not), and where its firmware files live.


## 3. List of Repositories

| Repo | Builds | What it's for |
|---|---|---|
| `kernel-adaptation-<device>` | `kernel-adaptation-<device>` | Kernel, modules and device tree |
| `droid-hal-img-boot-<device>` | `droid-hal-<device>-img-boot` | The boot image, using the standard Sailfish initrd |
| `droid-firmware-<device>` | `droid-firmware-<device>` | Firmware files for the kernel |
| `droid-config-<device>` | `droid-config-<device>-*`, patterns | Device configuration, the package list (patterns) and the image layout (kickstart) |
| `droid-hal-version-<device>` | `droid-hal-version-<device>` | Version package that ties a release together |

The `droid-` names are kept on purpose: the shared Sailfish templates and scripts look for them, so the templates work unchanged.

Each repo is mostly a spec file plus an upstream template, pulled in as a git submodule:

| Repo | Submodule | Upstream |
|---|---|---|
| kernel-adaptation | `linux` | your kernel tree |
| kernel-adaptation | `kernel-adaptation-simplified` | `github.com/VerdandiTeam/kernel-adaptation-simplified`, based on HelloVolla's. This copy has `helpers/build_packages.sh`, the HADK build script adapted for mainline ports. |
| droid-hal-img-boot | `initrd` | `github.com/mer-hybris/hybris-initrd` |
| droid-config | `droid-configs-device` | `github.com/mer-hybris/droid-hal-configs` |
| droid-hal-version | `droid-hal-version` | `github.com/mer-hybris/droid-hal-version` |

Some of these say "hybris" for historical reasons. Built with `native_build`, none of them needs libhybris.

Plus one tool, not packaged: **mer-kernel-check** (`github.com/mer-hybris/mer-kernel-check`), which checks your kernel config against what Sailfish OS needs.

The build order follows the dependencies:

1. kernel
2. boot image (needs the kernel)
3. firmware
4. configs (the patterns need all of the above)
5. version (needs everything)
6. root filesystem image


## 4. Setting up the SDK

Install the Platform SDK as the Sailfish OS docs describe. Then set up your environment, the same way as in the HADK:

**PLATFORM SDK**
```sh
cat <<'EOF' > $HOME/.hadk.env
export ANDROID_ROOT="$HOME/mainline"
export VENDOR="<vendor>"
export DEVICE="<device>"
export PORT_ARCH="<aarch64|armv7hl>"
export RELEASE="<Sailfish OS release, e.g. 5.2.0.22>"
EOF

cat <<'EOF' >> $HOME/.bashrc
if [[ $SAILFISH_SDK ]]; then
  function hadk() { source $HOME/.hadk.env; echo "Env setup for $DEVICE"; }
  hadk
fi
EOF
```

`ANDROID_ROOT` is just the folder that holds your repos. The name stays because the scripts use it.

Install a tooling and a target for your release with `sdk-assistant` (Sailfish OS docs: Tools > Platform SDK > Target Installation). Name the target `$VENDOR-$DEVICE-$PORT_ARCH`, because `build_packages.sh` always builds in a target with that name.

> **Note:** If creating the target fails and sb2 then says "Invalid target", your Platform SDK chroot is probably older than the tooling. The tooling's compiler needs a newer glibc than the chroot has, so update the chroot.


## 5. Packaging the Kernel

Create the repo:

**PLATFORM SDK**
```sh
mkdir -p $ANDROID_ROOT/kernel-adaptation-$DEVICE && cd $ANDROID_ROOT/kernel-adaptation-$DEVICE
git init
git submodule add <your kernel git url> linux
git submodule add https://github.com/VerdandiTeam/kernel-adaptation-simplified kernel-adaptation-simplified
cp <a kernel config that boots on your device> sfos-${DEVICE}_defconfig
mkdir rpm ramdisk
touch ramdisk/.keep
```

Write `rpm/kernel-adaptation-<device>.spec`:

```spec
%define device <device>
%define kernel_arch <arm|arm64>
%define device_target_cpu <armv7hl|aarch64>

%define extra_config sfos-<device>_defconfig
%define defconfig %{extra_config}
%define source_directory linux

%define build_modules 1
%define build_Image 1
%define kernel_image <zImage|Image.gz>

%define ramdisk_directory ramdisk

%define deviceinfo_dtb <vendor dir>/<board>.dtb
%define deviceinfo_append_dtb <true|false>
%define deviceinfo_kernel_cmdline <kernel cmdline>
%define deviceinfo_flash_pagesize <page size>
%define deviceinfo_flash_offset_base <base>
%define deviceinfo_flash_offset_kernel <offset>
%define deviceinfo_flash_offset_ramdisk <offset>
%define deviceinfo_flash_offset_second <offset>
%define deviceinfo_flash_offset_tags <offset>
%define deviceinfo_bootimg_header_version <0|1|2>
%define deviceinfo_bootimg_os_version 0
%define deviceinfo_bootimg_os_patch_level 0

Version:        <kernel version>
Release:        1

Provides:       droid-hal-<device>-kernel
Provides:       droid-hal-<device>-kernel-modules

%include kernel-adaptation-simplified/kernel-adaptation-simplified.inc
```

What it means:

- `extra_config`: the build copies this file from the repo root into `linux/arch/<arch>/configs/` before running make. Edit only the root copy, because the one in `linux/` gets overwritten.
- `deviceinfo_*`: these mean the same as in a postmarketOS deviceinfo. Take the offsets, page size and header version from your stock boot image (for example with `unpack_bootimg`) or from your device's postmarketOS deviceinfo.
- `ramdisk_directory`: besides the kernel, the .inc always builds a plain `boot.img` and needs a ramdisk for it. You don't flash that image (the boot image package in chapter 6 builds the real one), so an almost empty folder will do.
- The two `Provides` let the boot image package find the kernel.

> **Note:** The kernel builds out of tree (`O=out`), so the `linux` checkout must be clean. If make complains about it, run `make mrproper` in `linux/`.

> **Tip:** victara keeps a small debug initramfs (from hybris-boot, with telnet over USB) in `ramdisk/`. The kernel package's own `boot.img` then gives a quick way to test a new kernel without building a whole image.

### Kernel config

Run mer-kernel-check on the full config. Expand it with `olddefconfig` first, or options that are on by default show up as missing:

**PLATFORM SDK**
```sh
cd $ANDROID_ROOT
git clone https://github.com/mer-hybris/mer-kernel-check
mkdir -p /tmp/k && cp kernel-adaptation-$DEVICE/sfos-${DEVICE}_defconfig /tmp/k/.config
make -C kernel-adaptation-$DEVICE/linux O=/tmp/k ARCH=<arm|arm64> olddefconfig
perl mer-kernel-check/mer_verify_kernel_config /tmp/k/.config
```

- Fix every ERROR.
- Go through the WARNINGs. Each one comes with a comment that says why it's there, and some only matter for hybris ports (bluebinder, the old hybris-boot init, SELinux for Android).

The checker doesn't tell you about these, but you need them too:

| Option | Why |
|---|---|
| `CONFIG_MODULE_COMPRESS_XZ=y` | Sailfish's kmod has no zstd support (only xz and zlib). With zstd-compressed modules, no module ever loads. |
| `# CONFIG_GCC_PLUGINS is not set` | The SDK's gcc can't load the kernel's gcc plugins, so the build fails. |
| `# CONFIG_BTRFS_FS is not set`, `# CONFIG_CRC_OPTIMIZATIONS is not set`, `# CONFIG_DM_INTEGRITY is not set` | armv7 on 7.x kernels: these pull in NEON code that doesn't build under the SDK's scratchbox2. |
| `CONFIG_BLK_DEV_DM=y`, `CONFIG_DM_CRYPT=y` | The initrd mounts root and home from LVM. DM_CRYPT is for home encryption. |
| `CONFIG_USB_CONFIGFS=y`, `CONFIG_USB_CONFIGFS_RNDIS=y` | USB networking that works without modules. |

### Build it

**PLATFORM SDK**
```sh
cd $ANDROID_ROOT
H=kernel-adaptation-$DEVICE/kernel-adaptation-simplified/helpers/build_packages.sh
$H -k
```

Packages land in `$ANDROID_ROOT/droid-local-repo/$DEVICE`, and build logs in `$ANDROID_ROOT/<package>.log`.

> **Warning:** `-k` deletes the old kernel RPMs before it builds. Keep a copy of a known good one.


## 6. Packaging the Boot Image

This package builds the boot image with **hybris-initrd**, the same initrd Sailfish's official devices use. It needs nothing from Android. It mounts root and home from LVM, resizes them on first boot, does factory reset and shows the boot screen.

**PLATFORM SDK**
```sh
mkdir -p $ANDROID_ROOT/droid-hal-img-boot-$DEVICE/rpm && cd $ANDROID_ROOT/droid-hal-img-boot-$DEVICE
git init
git submodule add https://github.com/mer-hybris/hybris-initrd initrd
cp ../kernel-adaptation-$DEVICE/kernel-adaptation-simplified/mkbootimg.py .
```

Pin `initrd` to a commit that builds against your release. Master can need newer packages (as of 2026-11 it wants `openssh-server >= 10`).

Write `<device>-mkbootimg.sh`. It packs the kernel and the initrd into a boot image your bootloader accepts. The kernel files come from the kernel package, which gets installed in the build target. For an Android-style bootloader, with the same values as the `deviceinfo_*` macros:

```sh
#!/bin/sh
set -e
INITRD=$1
OUT=$2
CMDLINE='<kernel cmdline>'
DTB=<board>.dtb

# Only if your bootloader wants the DTB appended to the kernel (Image.gz on arm64):
cat /boot/zImage /boot/$DTB > zImage-dtb

python3 mkbootimg.py --kernel zImage-dtb --ramdisk "$INITRD" \
    --base <base> --kernel_offset <offset> --ramdisk_offset <offset> \
    --second_offset <offset> --tags_offset <offset> --pagesize <page size> \
    --header_version <version> --cmdline "$CMDLINE" --output "$OUT"
```

Write `rpm/droid-hal-<device>-img-boot.spec`:

```spec
%define device <device>
%define native_build 1
%define disable_tools 1

%define mkbootimg_cmd sh <device>-mkbootimg.sh %{initrd}

%define root_part_label <partition for Sailfish, e.g. userdata>
%define lvm_root_size <root size in MB, e.g. 5000>

%define display_brightness_path /sys/class/backlight/<backlight>/brightness
%define display_brightness 100

BuildRequires:  python3-base

%include initrd/droid-hal-device-img-boot.inc
```

- `native_build`: take the kernel from `kernel-adaptation-<device>` instead of droid-hal packages.
- `disable_tools`: don't require droid-hal-tools, the flashing tools that come from an Android tree.
- `mkbootimg_cmd`: the .inc adds the output file name as the last argument, then runs the command for the boot image and again for the recovery image.
- `root_part_label`: the partition label of the partition that holds Sailfish (the LVM physical volume).
- `lvm_root_size`: on first boot the initrd grows LVM to the whole partition, gives root this size and the rest to home.
- `display_brightness_*`: the backlight the initrd turns on for its own screens.

The package also carries its own copy of the kernel modules. That's why the image needs only this package and not the kernel package.

**PLATFORM SDK**
```sh
cd $ANDROID_ROOT && $H -b droid-hal-img-boot-$DEVICE
```


## 7. Packaging the Firmware

Sailfish OS has no linux-firmware package, so anything the kernel loads from `/lib/firmware` has to come from your own package. Only ship files you're allowed to redistribute (check linux-firmware's `WHENCE` file), and include their licence.

**PLATFORM SDK**
```sh
mkdir -p $ANDROID_ROOT/droid-firmware-$DEVICE/{rpm,firmware} && cd $ANDROID_ROOT/droid-firmware-$DEVICE
git init
# copy the files into firmware/, with the paths they need under /lib/firmware/
```

Write `rpm/droid-firmware-<device>.spec`:

```spec
Name:       droid-firmware-<device>
Version:    1
Release:    1
Summary:    Firmware for <device>
License:    <licences of the files>
BuildArch:  noarch
Source0:    %{name}-%{version}.tar.bz2

%description
%{summary}.

%prep
%autosetup -n %{name}-%{version}

%build

%install
mkdir -p %{buildroot}/lib/firmware
cp -a firmware/* %{buildroot}/lib/firmware/

%files
/lib/firmware/*
```

Firmware that lives on a partition of the device (common on Qualcomm phones) shouldn't be packaged. See [Firmware on a partition](#firmware-on-a-partition).

**PLATFORM SDK**
```sh
cd $ANDROID_ROOT && $H -b droid-firmware-$DEVICE
```


## 8. Packaging the Configs

**PLATFORM SDK**
```sh
mkdir -p $ANDROID_ROOT/droid-configs/{rpm,patterns,sparse} && cd $ANDROID_ROOT/droid-configs
git init
git submodule add https://github.com/mer-hybris/droid-hal-configs droid-configs-device
git -C droid-configs-device checkout <commit that matches your release>
cp droid-configs-device/droid-config-@DEVICE@.spec.template rpm/droid-config-$DEVICE.spec
for p in adaptation configuration; do
  cp droid-configs-device/patterns/templates/patterns-sailfish-device-$p-@DEVICE@.inc patterns/patterns-sailfish-device-$p-$DEVICE.inc
done
```

The folder must be called `droid-configs`, because `build_packages.sh` looks for it by that name.

> **Note:** The template follows the newest Sailfish release, and newer commits can rely on changes your release doesn't have. victara on 5.2 uses `a9b3995` (as of 2026-11).

### Spec

In the spec and both pattern files, replace `@DEVICE@`, `@VENDOR@`, `@VENDOR_PRETTY@` and `@DEVICE_PRETTY@`. Then add to the spec:

```spec
%define native_build 1
%define disable_tools 1
```

- `native_build` is the main switch. It drops droid-hal, the Android headers and the Android audio modules from the config packages.
- `pixel_ratio` sets the UI scale. For 16:9 phones of 4 to 5.5 inches, the template suggests `4.5 / diagonal in inches * horizontal pixels / 540`, in steps of 0.25.

> **Important:** A `%define` stays defined even behind a `#`. To switch one off, delete its `%` too.

### Dropping the Android init

The common template ships the units that start Android's init. List them in `delete_file.list` so they stay out of the image:

```
/usr/bin/droid/droid-hal-startup.sh
/usr/lib/systemd/system/droid-hal-init.service
/usr/lib/systemd/system/droid-late-start.service
/usr/lib/systemd/system/graphical.target.wants/droid-late-start.service
```

### Patterns

`patterns-sailfish-device-adaptation-<device>.inc` is the list of device packages in the image. The template is written for hybris ports.

Remove the Android and libhybris packages:

- `droid-hal-<device>`
- `droid-hal-<device>-kernel-modules` (the boot image package carries the modules)
- `libhybris-*`
- `hybris-libsensorfw-qt5`
- `ngfd-plugin-native-vibrator`
- `pulseaudio-modules-droid*`
- `qt5-qpa-hwcomposer-plugin`
- `gstreamer1.0-droid`
- `geoclue-provider-hybris`

Add the native ones:

```spec
Requires: droid-firmware-<device>

Requires: mesa-dri-drivers
Requires: mesa-libEGL
Requires: mesa-libGLESv2
Requires: mesa-libgbm
Requires: wayland-egl
Requires: qt5-plugin-platform-eglfs

Requires: qt5-feedback-haptics-ffmemless
Requires: pulseaudio-module-keepalive
Requires: alsa-ucm-conf
```

Keep the rest: `droid-hal-<device>-img-boot`, the `droid-config-<device>-*` packages, `droid-hal-version-<device>`, `usb-moded`, `mce-plugin-libhybris` and so on.

- `pulseaudio-module-keepalive`: Sailfish's PulseAudio config loads `module-keepalive`. droid-configs pulls this package in for native builds, in place of `pulseaudio-modules-droid`.
- Image type: because the pattern requires the boot image package and not the kernel package, `build_packages.sh -i` builds a loop image, which your pack script turns into flashable images. A pattern that requires `kernel-adaptation-<device>` gets a rootfs tarball instead.

### Minimum sparse files

Everything under `sparse/` is copied into the image as is. A first image needs two files.

`sparse/usr/share/ssu/features.d/native-common.ini` adds the repo that has Mesa and other native packages, both to the device and to the image kickstart:

```ini
[repositories]
native-common=https://repo.sailfishos.org/obs/nemo:/devel:/hw:/native-common/sailfish_latest_<arch>/
```

Adding the repo to the SDK target with `ssu ar` isn't enough, because the kickstart is generated from the ssu config inside droid-configs.

`sparse/var/lib/environment/compositor/droid-hal-device.conf` makes lipstick run on eglfs with KMS instead of hwcomposer:

```sh
EGL_PLATFORM=drm
QT_QPA_PLATFORM=eglfs
QT_QPA_EGLFS_INTEGRATION=eglfs_kms
QT_QPA_EGLFS_PHYSICAL_WIDTH=<panel width in mm>
QT_QPA_EGLFS_PHYSICAL_HEIGHT=<panel height in mm>
```

### Kickstart

Three files under `kickstart/` decide how the image is put together. Without them the template uses its hybris defaults, which build a recovery-flashable zip with updater scripts. Your own files are copied over the defaults.

`kickstart/part/<device>` lists the partitions mic creates:

```
part / --fstype="ext4" --size=4000 --label=root
part /home --fstype="ext4" --size=800 --label=home
```

`kickstart/attachment/<device>` lists the files from the image's `/boot` that go into the zip:

```
/boot/hybris-boot.img
```

`kickstart/pack/<device>/hybris` is the script mic runs at the end. It turns `root.img` and `home.img` into a sparse `userdata` image holding the LVM volume group `sailfish` with volumes `root` and `home`, which is the layout the initrd expects. Then it writes a flash script and zips everything. This is victara's script, trimmed to a single partition:

```sh
pushd $IMG_OUT_DIR

DEVICE_VERSION_FILE=./hw-release
if [[ -a $DEVICE_VERSION_FILE ]]; then
  source $DEVICE_VERSION_FILE
  DEVICE_ID=-${MER_HA_DEVICE// /_}-$VERSION_ID
fi
source ./os-release
RELEASENAME=${NAME// /_}-$VERSION_ID$DEVICE_ID

# Shrink an ext4 image to its content and report its size
resizeloop() {
  local IMG=$1
  local LOOP=$(/sbin/losetup -f)
  /sbin/losetup $LOOP $IMG
  /sbin/e2fsck -f -y $LOOP
  local BLOCKCOUNT=$(/sbin/dumpe2fs -h $LOOP 2>&1 | grep "Block count:" | grep -o -E '[0-9]+')
  local FREEBLOCKS=$(/sbin/dumpe2fs -h $LOOP 2>&1 | grep "Free blocks:" | grep -o -E '[0-9]+')
  BLOCKSIZE=$(/sbin/dumpe2fs -h $LOOP 2>&1 | grep "Block size:" | grep -o -E '[0-9]+')
  local IMAGEBLOCKS=$(/usr/bin/expr $BLOCKCOUNT - $FREEBLOCKS)
  local IMAGESECTORS=$(/usr/bin/expr $IMAGEBLOCKS \* $BLOCKSIZE / 512)
  /sbin/resize2fs $LOOP ${IMAGESECTORS}s -f
  IMGBLOCKS=$(/sbin/dumpe2fs -h $LOOP 2>&1 | grep "Block count:" | grep -o -E '[0-9]+')
  IMGSIZE=$(/usr/bin/expr $IMGBLOCKS \* $BLOCKSIZE)
  IMGSECTORS=$(/usr/bin/expr $IMGBLOCKS \* $BLOCKSIZE / 512)
  /sbin/losetup -d $LOOP
}

resizeloop root.img
ROOTSIZE=$IMGSIZE; ROOTBLOCKS=$IMGBLOCKS; ROOTSECTORS=$IMGSECTORS
resizeloop home.img
HOMESIZE=$IMGSIZE; HOMEBLOCKS=$IMGBLOCKS; HOMESECTORS=$IMGSECTORS

# The userdata image only has to hold root and home. The initrd grows it to the partition on first boot.
USERDATA_SECTORS=$(/usr/bin/expr $ROOTSECTORS + $HOMESECTORS + 200000)
dd if=/dev/zero bs=512 count=0 of=userdata.img seek=$USERDATA_SECTORS
DATA_LOOP=$(/sbin/losetup -f --show userdata.img)

/usr/sbin/pvcreate $DATA_LOOP
/usr/sbin/vgcreate sailfish $DATA_LOOP
/usr/sbin/lvcreate -L ${ROOTSIZE}B --name root sailfish $DATA_LOOP
/usr/sbin/lvcreate -L ${HOMESIZE}B --name home sailfish $DATA_LOOP
/bin/sync
/usr/sbin/vgchange -a y sailfish
dd if=root.img bs=$BLOCKSIZE count=$ROOTBLOCKS of=/dev/sailfish/root
dd if=home.img bs=$BLOCKSIZE count=$HOMEBLOCKS of=/dev/sailfish/home
/bin/sync
/usr/sbin/vgchange -a n sailfish
/sbin/losetup -d $DATA_LOOP
rm home.img root.img

/usr/bin/img2simg userdata.img sailfish-userdata.img
rm userdata.img

cat > flash.sh <<'EOF_FLASH'
#!/bin/bash
set -e
cd "$(dirname "$0")"
fastboot flash boot hybris-boot.img
fastboot flash userdata sailfish-userdata.img
EOF_FLASH
chmod 755 flash.sh

mkdir -p ${RELEASENAME}
mv *.img* *-release flash.sh ${RELEASENAME}/
cd ${RELEASENAME}
md5sum * > md5.lst
cd ..
zip -r ${RELEASENAME}.zip $RELEASENAME
rm -r ${RELEASENAME}
popd
```

Change the `fastboot` lines to your device's partitions. The `userdata` one has to be the partition named in `root_part_label`.

**PLATFORM SDK**
```sh
cd $ANDROID_ROOT && $H -c
```


## 9. Packaging the Version

**PLATFORM SDK**
```sh
mkdir -p $ANDROID_ROOT/droid-hal-version-$DEVICE/rpm && cd $ANDROID_ROOT/droid-hal-version-$DEVICE
git init
git submodule add https://github.com/mer-hybris/droid-hal-version droid-hal-version
```

Write `rpm/droid-hal-version-<device>.spec`:

```spec
%define device <device>
%define vendor <vendor>
%define vendor_pretty <Vendor>
%define device_pretty <Device name>

%define native_build 1
%define have_ffmemless 1
%define have_led 1

%include droid-hal-version/droid-hal-version.inc
```

- `native_build`: no libhybris, droid-hal or hwcomposer dependencies.
- `have_ffmemless`: vibration through ff-memless.
- `have_led`: the device has a notification LED. Leave it out if yours doesn't.

**PLATFORM SDK**
```sh
cd $ANDROID_ROOT && $H -v
```


## 10. Creating the Sailfish OS Root Filesystem

**PLATFORM SDK**
```sh
cd $ANDROID_ROOT && $H -i
```

mic builds the image from the kickstart the configs build generated, with your local repo as the adaptation repo. The zip from your pack script ends up in `$ANDROID_ROOT/SailfishOScommunity-release-$RELEASE-$DEVICE/`. The template defines `community_adaptation`; without it, the folder name has no "community".

Rebuilds go in the same order. After changing only configs, `$H -c -v -i` is enough.

Patched middleware (Mesa, lipstick, sensorfw, ...) builds the same way, with `$H -b <folder>` or `$H --mw=<git url>`, and lands in the local repo too. Give it a higher version than the copy in the Sailfish repos, or the image keeps the stock package.


## 11. Flashing the rootfs image

Unzip the release and run `flash.sh` with the device in fastboot mode:

**HOST**
```sh
unzip Sailfish_OS-*.zip
cd Sailfish_OS-*/
./flash.sh
```

On first boot the initrd resizes the LVM volumes, then the Sailfish startup wizard runs.


## 12. Getting In

### Get connected

On hybris ports, Android's init creates the USB gadget, and usb-moded only switches it between modes. On a native port nothing creates it, so there's no USB networking until you add two things to droid-configs.

1. **A oneshot service that creates the gadget**, ordered before `usb-moded.service`. It creates gadget `g1` in configfs with an RNDIS function and a config `c.1`, and binds it to the UDC. victara took `setup-configfs.service` and `setup-configfs.sh` from the Sailfish PinePhone port (droid-config-pinephone).
2. **usb-moded settings** in `sparse/etc/usb-moded/<anything>.ini` that describe that gadget:

   ```ini
   [configfs]
   gadget_base_directory = /sys/kernel/config/usb_gadget/g1
   function_rndis        = rndis.usb0
   function_mtp          = ffs.mtp
   gadget_conf_directory = configs/c.1
   gadget_udc_device     = <your UDC, see ls /sys/class/udc>

   [network]
   interface = usb0
   ```

Also set up cable detection ([usb-moded doesn't notice the cable](#usb-moded-doesnt-notice-the-cable-or-always-picks-charging-only)). Then turn on developer mode in Settings, and the device is at `192.168.2.15` over USB:

**HOST**
```sh
ssh defaultuser@192.168.2.15
```

Use `devel-su` for root.

### Logs across reboots

Give your device tree a ramoops region. After a crash and a warm reset, `/sys/fs/pstore/` then holds the previous boot's kernel log. On victara a long-press power-off wipes it, so let the phone reset by itself if you can.

Otherwise `journalctl` and `dmesg` work as on any Linux system. There's no Android logcat.


## 13. Detailed subsystem adaptation guides

What we ran into on victara after the first image, by subsystem. Each entry: what you see, why it happens, how we fixed it.

### USB

#### usb-moded doesn't notice the cable, or always picks "charging only"

On hybris ports usb-moded follows the Android USB driver, which a mainline kernel doesn't have. Without a path, it reads every extcon device it finds. On victara an unused ID-pin detector reported "no USB", so usb-moded treated the PC as a wall charger.

We pointed usb-moded at the extcon of the charger driver:

```ini
[udev]
extcon_path = /sys/class/extcon/<extconN>
android_tracking = 0
extcon_tracking = 1
```

Find the right one with `cat /sys/class/extcon/*/name` and `state`, with the cable in and out. The extconN numbers aren't guaranteed to stay the same, so we also disabled the unused extcon device in the device tree.

### Charging and battery

#### No battery percentage

Sailfish reads the charge level from a battery power supply's `capacity`. Many SoC charger drivers don't provide one, and you need the fuel gauge driver. For victara we had to write one (PM8941 BMS).

#### The UI always says "charging"

mce combines every power supply it finds. On victara the charger's battery-interface node kept reporting "fast charge". We hid it from mce in an mce ini:

```ini
[BatteryUDevDeviceBlacklist]
<power supply name>=true
```

A group you define replaces mce's built-in list, so list everything you want hidden.

#### Charging is slow

If the charger driver can't tell a PC port from a wall charger, it stays at the USB default of 500 mA. We raise the input limit to 1.5 A with a udev rule that writes `charge_control_limit` on the USB power supply. That's a trade-off, since a PC port only promises 500 mA.

#### The charging modes in Settings do nothing

mce stops and starts charging through a sysfs switch, and you have to tell it where that switch is:

```ini
[Charging]
ControlPath=<sysfs file that enables charging>
EnableValue=1
DisableValue=0
```

victara's charger driver had no such switch, so we gave it a `charging_enabled` module parameter.

### Notification LED

#### The LED never lights up

mce drives LEDs through `mce-plugin-libhybris`. Despite the name, it also has plain sysfs backends, and one has to be configured in an mce ini:

```ini
[LEDConfigHybris]
BackEnd=<backend that fits your LED>
LedDirectory=/sys/class/leds/<led name>
```

victara's LED is a single green one, so we use `white`. Don't give the LED a `linux,default-trigger` in the device tree, because mce wants to drive it itself.

### Vibration

#### ngfd logs "Vibra upload effect: No space left on device"

ngfd's ff-memless config uploads more effects (17) than the kernel's ff-memless has slots (16). We raised `FF_MEMLESS_EFFECTS` to 64 in `drivers/input/ff-memless.c`.

#### Touch feedback is too weak to feel

The Qt feedback plugin's default pulses (14 ms for buttons, 6 ms for the keyboard) were too short for victara's motor to get going. We override them in `/etc/qt5feedback/ffmemless.ini` (`BUTTON_PRESS_DURATION`, `KEYPAD_PRESS_DURATION` and their strengths), and point the session at that file with `FF_MEMLESS_SETTINGS=/etc/qt5feedback/ffmemless.ini` in `/var/lib/environment/nemo/<anything>.conf`. victara uses 50 ms and 40 ms.

Both the Qt plugin and ngfd find the vibra input device on their own.

### Audio

On a native port, audio goes through four layers:

1. the kernel sound card (ALSA)
2. an ALSA UCM profile, which says which mixer switches make the speaker or mic work
3. PulseAudio, which turns each UCM device into a port
4. Sailfish's audio policy (`xpolicy.conf`), which picks the port for each situation: speaker, headset, call

#### PulseAudio has no sound card

The template's PulseAudio config loads `module-droid-card`, the Android audio HAL. If `/etc/pulse/arm_droid_card_custom.pa` exists, it loads that file instead. We ship one with just:

```
load-module module-udev-detect
```

We use udev-detect rather than a fixed card because victara's DSP-based card registers late, and udev-detect picks it up whenever it appears.

#### The card is there but stays silent

Phone sound cards (ASoC) need mixer routes set before any audio comes out, and PulseAudio learns those routes from a UCM profile. Name the profile after your card's driver, which is shown after the brackets in `cat /proc/asound/cards`.

`/usr/share/alsa/ucm2/conf.d/<driver>/<driver>.conf`:

```
Syntax 2

SectionUseCase."HiFi" {
	File "/conf.d/<driver>/HiFi.conf"
	Comment "Play HiFi quality Music"
}
```

`/usr/share/alsa/ucm2/conf.d/<driver>/HiFi.conf`:

```
SectionVerb {
	Value {
		TQ "HiFi"
	}
}

SectionDevice."Speaker" {
	Comment "Speaker"
	EnableSequence [
		cset "name='<mixer control>' 1"
	]
	DisableSequence [
		cset "name='<mixer control>' 0"
	]
	Value {
		PlaybackPriority 100
		PlaybackPCM "hw:${CardId},0"
	}
}
```

Add a `SectionDevice` for each output and input you have (headphones, mic, ...). To build the profile:

1. Find the mixer controls that make each output work with `amixer -c0 contents` and `speaker-test`.
2. Test the profile with `alsaucm -c <card> set _verb HiFi set _enadev Speaker`.

#### PulseAudio works, but Sailfish plays nothing

Sailfish's policy picks sinks and ports by name, and the template's `xpolicy.conf` uses the Android card's names (`droid.output.primary`, `output-speaker`). Nothing matches, so streams have nowhere to go.

We took the `xpolicy.conf` from the Sailfish PinePhone port, which is written for an ALSA card, and changed only its `[variable]` block:

- the card: `alsa_card.*` and `alsa.card_name`
- the profile: the UCM verb (`HiFi`)
- the ports for each role: the UCM device names as PulseAudio shows them (`[Out] Speaker`, `[In] Mic`)

Roles you don't have yet (earpiece, headset) can point at the speaker. Get the real names from `pactl list cards` and `pactl list sinks`.

#### Bluetooth audio stays on the speaker

There were two causes:

- The Bluetooth policy files (`xpolicy.conf.d/bluez5*.conf`) needed the same renames.
- The droid-configs-device template needs the ohm `dbus_method` handlers (commit `aceec5c`). Without them ohmd answers PulseAudio's route change with "UnknownMethod", and A2DP is never used. Our pin `a9b3995` has them.

### Display and graphics

#### There's no Mesa driver for your GPU

Check that native-common's Mesa builds the Gallium driver for your GPU on your architecture. freedreno (Adreno) used to be commented out for ARM. We enabled it, and that build is now in native-common, so Adreno devices get it from there. If your GPU's driver is still off, rebuild Mesa the same way (`%global with_<driver> 1`) with a higher release number.

#### Touches arrive twice (no scrolling, stuck highlights)

This happened with `-plugin evdevtouch` in `LIPSTICK_OPTIONS` on top of eglfs, which already reads input itself. Remove the plugin.

#### Screenshots come out without app backgrounds

With Mesa the EGL config has an alpha channel. lipstick saved the grab as an image with alpha, and saving it wiped every transparent pixel. Our lipstick fix treats the grabbed image as opaque (`github.com/Michal-Szczepaniak/lipstick`, branch `fix-mesa-screenshots`).

#### The browser crashes on start (freedreno, Mesa 25.1.x)

The cause is a fence race in freedreno, fixed upstream in Mesa 26.1 by `b587181f` ("freedreno: Check for flushed batches"). We backport that commit to 25.1.8.

#### The home unlock screen is black (command-mode panels)

The unlock UI draws into the fbdev device. On a command-mode panel, a new picture only reaches the screen when the driver sends a frame, and msm's fbdev emulation never did that for those writes. We changed msm's fbdev mmap to deferred I/O, which turns the writes into frame updates. If your unlock screen stays black, check your DRM driver's fbdev emulation.

#### Videos look stretched

Without gst-droid, QtMultimedia uses its generic video renderer, which honours `fillMode: Stretch`. The hybris video backend ignores that setting, so apps that use it look fine on hybris ports. It's not a bug in your port.

#### The boot image is too big for the boot partition

victara's boot image is about 13.5 MB (half of it the initrd with LVM and the boot screen), more than its bootloader setup allows. Its bootloader can boot from a filesystem (extlinux), so the boot image package also installs the kernel, DTB, initrd and an `extlinux.conf` into a small ext2 partition mounted on `/boot`. Package updates then land where the bootloader reads them.

### Firmware

#### Firmware on a partition

Modem, DSP and Wi-Fi firmware on Qualcomm phones lives on a partition of the device, so we don't package it. We mount the partition early and point the kernel's firmware loader at it, from a oneshot service that runs before udev:

```sh
#!/bin/sh
mkdir -p /firmware
mount -o ro,nosuid,nodev <partition device> /firmware
echo -n /firmware/<subfolder> > /sys/module/firmware_class/parameters/path
```

```ini
[Unit]
DefaultDependencies=no
Before=systemd-udevd.service
RequiresMountsFor=/sys /dev

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/bin/<device>-firmware-mount

[Install]
WantedBy=sysinit.target
```

- A `.mount` unit can't do this, because it can't be ordered before udev (its device unit needs udev).
- Before udev there are no `/dev/disk/by-partlabel` links either. Use the plain device node, or find the partition by `PARTNAME=` in `/sys/class/block/*/uevent`.
- The kernel tries `firmware_class.path` first and `/lib/firmware` after it, so packaged and partition firmware work side by side.
- It's the same idea as postmarketOS's msm-firmware-loader.

### Wi-Fi and Bluetooth

#### The kernel can't load regulatory.db

wireless-regdb installs to `/usr/lib/firmware`, but the kernel only looks in `/lib/firmware`. We symlink `/lib/firmware/regulatory.db` and `regulatory.db.p7s` to `/usr/lib/firmware/`.

#### Wi-Fi disconnects on every suspend

Without a WoWLAN configuration, the kernel disconnects Wi-Fi when the phone suspends. We turn it on with a udev rule:

```
ACTION=="add", SUBSYSTEM=="ieee80211", RUN+="/usr/sbin/iw phy %k wowlan enable any"
```

#### Saved Wi-Fi networks disappear, or the MAC or Bluetooth address is random

connman keys saved networks by MAC address, so they vanish whenever the address changes. There are two ways to fix it:

- **Device tree:** give the kernel the real address (`local-mac-address`, `local-bd-address`, often filled in by the bootloader). This is the cleaner way.
- **Userspace:** set the address when the interface appears. That races with connman. victara does this one, with udev-started services that read the factory data (`ip link set ... address` for Wi-Fi, `btmgmt public-addr` for Bluetooth).

### Sensors

#### No rotation, light or proximity sensor

The hybris sensorfw adaptor reads the Android sensor HAL. On a native port, use sensorfw's IIO adaptors in `/etc/sensorfw/primaryuse.conf`:

```ini
[plugins]
accelerometeradaptor = iioaccelerometeradaptor
alsadaptor = iioalsadaptor
proximityadaptor = iioproximityadaptor

[accelerometer]
input_match = <IIO device name>
transformation_matrix = "1,0,0,0,1,0,0,0,1"
```

- `input_match` is the name from `cat /sys/bus/iio/devices/iio:device*/name`.
- The matrix turns the chip's axes into the phone's.

The stock adaptor couldn't read values from buffered IIO devices, so we use a sensorfw fork (affenull2345's `iio` branch, plus our fixes).

### Power

#### The CPU keeps jumping to its highest frequency

We found two causes:

- **Real-time tasks:** by default every real-time task makes schedutil jump to max, and Sailfish runs some daemons as real-time (dsme). `kernel.sched_util_clamp_min_rt_default = 0` in a `/usr/lib/sysctl.d/` file cut the time at max frequency about 6x.
- **Kernel bug on 32-bit:** on 7.x kernels, an overflow in the scheduler's load tracking (from `089d84203ad4`) kept idle cores at max frequency. We fixed it by making `r` a `u64` in `update_cfs_rq_load_avg()`.

### Encryption

#### A passphrase prompt appears on the console during boot

With home encryption, `systemd-ask-password-console` prints its prompt on the console and wipes the boot logo. The real unlock is done by Sailfish's unlock agent. We mask `systemd-ask-password-console.path` and `.service` (symlinks to `/dev/null` in `/etc/systemd/system/`).

On armv7, also set `CONFIG_CRYPTO_AES_ARM_BS=y` for NEON AES-XTS.

### Storage

#### Optional: more space for home

If the device has large partitions Sailfish doesn't use, like Android's `system` and `cache`, the pack script can add them to the `sailfish` volume group as extra physical volumes (one more sparse image per partition, flashed to that partition). On first boot the initrd gives all free space in the volume group to home. victara gains about 4 GB this way.

### Not solved yet

- **Calls, SMS and mobile data on Qualcomm modems:** the modem talks QMI (it needs `rmtfs` and `qrtr`), and Sailfish's ofono is built without its QMI driver.
- **Camera.**
- **Hardware video decoding.**


## 14. Hardware Adaptation Checklist

| Feature | Where |
|---|---|
| UI comes up | [Minimum sparse files](#minimum-sparse-files), [No Mesa driver](#theres-no-mesa-driver-for-your-gpu) |
| Touch | [Touches arrive twice](#touches-arrive-twice-no-scrolling-stuck-highlights) |
| USB networking and SSH | [Get connected](#get-connected) |
| USB mode switching | [usb-moded doesn't notice the cable](#usb-moded-doesnt-notice-the-cable-or-always-picks-charging-only) |
| Charging state and percentage | [Charging and battery](#charging-and-battery) |
| Notification LED | [Notification LED](#notification-led) |
| Vibration | [Vibration](#vibration) |
| Speaker and mic | [Audio](#audio) |
| Bluetooth audio | [Bluetooth audio](#bluetooth-audio-stays-on-the-speaker) |
| Wi-Fi, also across suspend | [Wi-Fi and Bluetooth](#wi-fi-and-bluetooth), [Firmware](#firmware) |
| Sensors | [Sensors](#sensors) |
| Battery life | [Power](#power) |
| Home encryption | [Encryption](#encryption), [kernel config](#kernel-config) |
| Screenshots, browser | [Display and graphics](#display-and-graphics) |
| Calls, SMS, data | [Not solved yet](#not-solved-yet) |
