# Sailfish OS Mainline HADK

Hardware adaptation guide for porting Sailfish OS to devices that run a mainline Linux kernel.

As of 2026-11-10. Written from the Moto X 2014 (victara) port: Sailfish OS 5.2.0.22, kernel 7.3.3, armv7hl.


## 1. Overview

The regular [HADK](https://hadk.sailfishos.org/) builds Sailfish OS on top of Android: an **Android base**, droid-hal and libhybris. A **mainline port** has none of that. The kernel's own drivers handle the hardware:

- graphics through Mesa
- audio through ALSA and PulseAudio
- sensors through IIO, or on Qualcomm phones with a Sensor Core through the ADSP's sensor service (see [Sensors](#sensors))
- vibration through the kernel's ff-memless input interface

The rest is the same as in the HADK: the Platform SDK, `build_packages.sh`, mic and kickstarts, and mostly the same package names. Instead of an Android tree you keep five small repos.

Two ports fed this guide. The walkthrough and most of chapter 13 come from victara (Moto X 2014, MSM8974, armv7hl, sensors on IIO, a 2014 bootloader). Entries marked **(Pro1-X)** come from the F(x)tec Pro1-X (Qualcomm SM6115, aarch64, Android bootloader with A/B slots, Sailfish OS 5.2.0.22 on kernel 7.3): the Qualcomm modem, sensors behind the DSP, call audio, camera and video. That port is not yet packaged as in chapter 3. Its config files, patches and package sources are at `github.com/moWerk/sailfishos-pro1x-mainline` and were tested installed by hand, so take file contents from it, not packaging.

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
| kernel-adaptation | `kernel-adaptation-simplified` | `github.com/sailfishos-open/kernel-adaptation-simplified`, based on HelloVolla's. It carries `helpers/build_packages.sh`, the HADK build script adapted for mainline ports. |
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
git submodule add https://github.com/sailfishos-open/kernel-adaptation-simplified kernel-adaptation-simplified
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

On a 7.x kernel these matter too. Each came from a mer-kernel-check error or from a Sailfish service failing on the Pro1-X (Sailfish OS 5.2, kernel 7.3) **(Pro1-X)**:

| Option | Why |
|---|---|
| `CONFIG_PM_AUTOSLEEP=y` | mce suspends the phone by writing to `/sys/power/autosleep`. Without it the file does not exist, mce still reports "Autosuspend policy: enabled", and the phone never suspends: screen off is CPU idle only. arm64 defconfig leaves it off. Check the success count in `/sys/kernel/debug/suspend_stats`. |
| `CONFIG_MEMCG_V1=y`, `CONFIG_CPUSETS_V1=y` | Sailfish's tmpfiles writes `/sys/fs/cgroup/memory/noswapping`. Without the cgroup v1 hierarchies `systemd-tmpfiles-setup` fails, `user@.service` never starts, and the boot ends without a UI. |
| `CONFIG_QUOTA_NETLINK_INTERFACE=y`, `CONFIG_QFMT_V2=y` | `quota_nld.service` fails without them. |
| `CONFIG_NETFILTER_XTABLES_LEGACY=y`, `CONFIG_IP_NF_IPTABLES_LEGACY`, `CONFIG_IP6_NF_IPTABLES_LEGACY`, with the filter, mangle, raw, nat, REJECT and rpfilter pieces | connman's firewall and tethering use the legacy iptables tables, which 7.x kernels hide behind these options. `=m` is fine, iptables loads them on demand. |
| `CONFIG_NET_L3_MASTER_DEV=y`, `CONFIG_INET_AH`, `CONFIG_INET_ESP`, `CONFIG_INET_IPCOMP`, `CONFIG_INET6_AH`, `CONFIG_INET6_ESP`, `CONFIG_PPP*`, `CONFIG_PPPOE`, `CONFIG_L2TP*` | the VPN plugins Sailfish ships. |
| `CONFIG_BT_RFCOMM`, `CONFIG_BT_BNEP`, and `CONFIG_BT_LE=y` on arm64 | bluez profiles. arm64 defconfig turns Bluetooth LE off, which leaves no BLE at all. |
| `CONFIG_NLS_UTF8=y` | vfat SD cards. |
| `CONFIG_QRTR=y`, `CONFIG_QRTR_SMD=y`, `CONFIG_QCOM_PD_MAPPER=y` (Qualcomm) | the QRTR name service and the protection-domain mapper live in the kernel now. Built in, they exist before the remoteprocs start, and no userspace `pd-mapper` is needed (an old one that is still started misses firmware mounted late). |
| `CONFIG_ZRAM=m` | Sailfish's `zram` package gives swap in RAM. Optional. |

Two rules the options do not express:

- **Everything between the storage and the panel is built in; drivers that load a file at probe are modules.** With an initrd that loads no modules, the storage controller and its PHY, the clock controllers, interconnect, RPM, pinctrl, DRM and the panel, and their I2C and DMA dependencies have to be `=y`. On the Pro1-X the display and GPU clock controllers, the GENI I2C controller and the GPI DMA as modules made mdss, adreno and the SMMU hit the deferred-probe timeout before udev could load them: the panel never lit and touch probed 47 s late. The other way round, a touch controller that fetches its configuration through the firmware loader (`goodix_9286_cfg.bin`) has to be a module, or the request fails before root is mounted.
- **If you generate the config, check that every line survived.** Kconfig silently demotes `=y` to `=m` or drops an option whose dependency is missing, and a config resolved on the unpatched tree loses every option the device patches introduce. The Pro1-X builds `arm64 defconfig` plus a device fragment plus a Sailfish fragment, runs `olddefconfig` on the patched tree, then fails the build on any fragment line that does not appear verbatim in `.config`. The first kernel without that check had lost the keyboard, haptics, battery and camera drivers.

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

3. **FunctionFS for MTP** **(Pro1-X)**. The `ffs.mtp` function only works when FunctionFS is mounted where buteo-mtp expects it, before usb-moded starts. The Pro1-X does it in the same oneshot:

   **DEVICE**
   ```sh
   mkdir -p /dev/mtp
   mount -t functionfs -o uid=0,gid=$(getent group mtp | cut -d: -f3),rmode=0770,fmode=0660 mtp /dev/mtp
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

The other way **(Pro1-X)**: a charger driver that registers a power supply named `usb` with `online` and `type` (`USB`, `USB_DCP`, `USB_CDP`). usb-moded's default tracking reads it, with no `[udev]` section at all. A Type-C port manager supply (tcpm) does not report a PC host as `online`, so point usb-moded at the charger's supply, not the Type-C one.

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

mce looks for the control file once, at start **(Pro1-X)**. If the driver that provides it loads later, the charge limit silently disappears from Settings. Order mce after it, for example an `ExecStartPre` drop-in on `mce.service` that waits for the path (the Pro1-X waits up to 30 s). Two more things from the Pro1-X: register a power supply named `usb` with `online` and `type` even when the charger driver only manages the input current, because usb-moded and mce read it, and let the battery supply follow it (`supplied_to`), so a plug change is reported at once instead of at the next poll; and report `Not charging` when input is present but the limit stopped the current, so the UI does not say charging while nothing flows.

### Notification LED

#### The LED never lights up

mce drives LEDs through `mce-plugin-libhybris`. Despite the name, it also has plain sysfs backends, and one has to be configured in an mce ini:

```ini
[LEDConfigHybris]
BackEnd=<backend that fits your LED>
LedDirectory=/sys/class/leds/<led name>
```

victara's LED is a single green one, so we use `white`. Don't give the LED a `linux,default-trigger` in the device tree, because mce wants to drive it itself.

#### The LED is one multicolor device (Pro1-X)

Mainline device trees describe an RGB indicator as one LED class device of the multicolor framework (`rgb:status` with `multi_intensity` and `multi_index`), while every backend of mce-plugin-libhybris expects one device per colour (`vanilla` takes `RedDirectory`, `GreenDirectory`, `BlueDirectory`). Either split the device tree into three LEDs, or add a backend: the Pro1-X carries `patches/mce-plugin-libhybris/0001-sysfs-led-add-multicolor-backend.patch` (`BackEnd=multicolor`, `LedDirectory=/sys/class/leds/rgb:status`), which writes the colour to `multi_intensity`, switches with `brightness`, and blinks with its own timer, because the PMIC LPG's hardware blink gives every channel the same duty cycle out of phase and a green blink shows a red, green, blue sweep. Not upstream.

### Vibration

#### ngfd logs "Vibra upload effect: No space left on device"

ngfd's ff-memless config uploads more effects (17) than the kernel's ff-memless has slots (16). We raised `FF_MEMLESS_EFFECTS` to 64 in `drivers/input/ff-memless.c`.

The Pro1-X keeps the stock kernel and trims the list instead **(Pro1-X)**: a file whose name ends in `ffmemless.ini` and sorts after `50-` (`/usr/share/ngfd/plugins.d/60-<device>-ffmemless.ini`) with `[ffmemless] supported_effects` listing 12 names or fewer (one of the stock 17 is a typo, `drag_boundary_drag_end`). Files with other names are not merged as plugin parameters, and `NGF_FFMEMLESS_SETTINGS` overrides only parameters, not the list. It also uses `libngf-qt5-qtfeedback` as the Qt feedback plugin instead of `qt5-feedback-haptics-ffmemless`, which bypasses ngfd and uploads 2-3 effects per process into the same 16 slots, and disables theme effects when an upload returns id 0 (which happens when ngfd died first). The trade-off: victara's kernel bump keeps the stock effect list and the Qt plugin's per-process effects; the Pro1-X route keeps a stock kernel. Motor strength is a driver parameter either way: an accepted `FF_RUMBLE` can still be too weak to feel. The Pro1-X ships `options aw86224 d2s_gain=7 drv2_lvl=100` in `/etc/modprobe.d/`.

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

#### PulseAudio 17 names UCM profiles differently (Pro1-X)

With PulseAudio 16 and later a UCM profile is called `<Verb> (<devices>)`, for example `HiFi (Mic, Speaker)` or `Voice Call (Earpiece, Mic)`, not `HiFi`. The `card_profile_*` variables in `xpolicy.conf` must carry the full name as `pactl list cards` prints it, and a card name with spaces or parentheses must be quoted, or every `[device]` rule silently mis-matches. The symptom on the Pro1-X was a call that connected without audio: the policy asked for a profile "Voice Call" that did not exist, so the card stayed in HiFi.

#### PulseAudio aborts: "Assertion 'dev == data->device' failed" (Pro1-X)

Jolla's `pulseaudio 17.0+git1` asserts in `pa_alsa_ucm_set_port()` when two UCM verbs declare a device of the same name, which is what you get once you add a `Voice Call` verb beside `HiFi` with the same Speaker, Earpiece and Mic. Upstream fixed it in January 2024 (`ed3d4f08`, "alsa-ucm: Replace port device UCM context assertion with an error"). piggz's `pulseaudio 17.0+git2+master` build in `nemo:devel:hw:pine:dontbeevil` has the fix; the Pro1-X uses it.

#### Rhythmic clicks on every route change: the policy loops (Pro1-X)

With UCM every route (speaker, earpiece, headset) is its own card profile, so a route change destroys the sink and creates a new one. Sailfish's `module-policy-enforcement` reports device availability to ohmd for every `[device]` rule; the sink removal is reported as "all built-in devices gone", ohmd reroutes, the new sink is reported back, ohmd reroutes again: the two profiles alternate every 0.18 s and you hear the codec clicking. Android-based ports never see it because the droid card has one profile and routes by port. Fix: the `disable_notify` flag on every `[device]` rule that matches the built-in card's sink:

```
[device]
type  = ihfandheadset
sink  = alsa.card_name@equals:$card_name
ports = alsa.card_name@equals:$card_name->$sink_headset
flags = disable_notify, $sink_port_change_delay
```

Found with `dbus-monitor --system` (the `com.nokia.policy` info signals) and `pactl subscribe` side by side.

#### No UI sounds, no camera shutter: ngfd started before PulseAudio (Pro1-X)

ngfd's canberra and tonegen backends connect to PulseAudio once at startup and never reconnect. Anything that delays PulseAudio (an ExecStartPre that waits for the sound card, a re-probe hook) lets ngfd start first and stay silent. Drop-in `ngfd.service.d/10-<device>.conf` in the user units: `[Unit]` `After=pulseaudio.service` and `Wants=pulseaudio.service`.

#### Call audio on Qualcomm DSP audio (q6) (Pro1-X)

Expect this to be the hardest part of a Qualcomm port. Mainline has the q6 ASoC drivers for media and no voice path. What works on the Pro1-X (earpiece and speaker, both directions, volume keys set the call volume; Bluetooth and the wired headset in calls still open):

1. **Kernel:** Stephan Gerhold's out-of-tree `q6voice` driver (CVP, CVS, MVM) with the voice front end `VoiceMMode1`, carried in the device kernel series (`CONFIG_SND_SOC_QDSP6_Q6VOICE=m`). It is his to upstream; do not send it separately.
2. **A `Voice Call` UCM verb** whose enable sequence runs a helper, and a `HiFi` verb for everything else. Sailfish's audio policy selects the verb through the `card_profile_voicecall*` variables (full names, see above).
3. **The helper** (`pro1x-voice-call`, from nyymi609's NixOS port, with oFono's `VoiceCallManager` instead of ModemManager): while oFono reports an active call it holds the hostless voice PCM open, which starts the DSP voice session, and plays and records silent keep-alive streams on the codec back ends so they stay powered. postmarketOS's `q6voiced` is the same idea.
4. **The keep-alive streams must belong to the call policy group.** Sailfish's policy classifies every stream; `gst-launch-1.0` is a "player", so each keep-alive moved the route to the speaker, the call moved it back, the sink vanished, the helper restarted it: a loop, heard as clicks. Tag the streams with a custom property and add an `xpolicy.conf` `[stream]` rule that maps it to group `call` (`media.name` is overwritten by the client library; custom properties survive). Verify `policy.group = "call"` on the sink input with `pactl list sink-inputs`.
5. **The codec DMA back ends stall on their first start in the call verb**: the playback pointer stays at 0, capture delivers zeros, the call is silent both ways although every mixer is set. One suspend and resume of the sink and the source after the keep-alive streams run restarts them; the helper does it once.
6. **Volume keys:** map Sailfish's call volume steps onto the codec's digital gain control, since PulseAudio sees no volume control on the DSP voice path.

The package with the helper, the verb and a README with the measurements is `packages/qx1050-voicecall` in the Pro1-X port. A pop or click from the speaker at stream start and stop is the amplifier switching before the codec; fix it in the amplifier driver (on after the codec, off before), not in userspace.

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

#### The modem and the DSPs fetch files at runtime (Qualcomm) (Pro1-X)

`/lib/firmware` is only the first half. Once the modem runs, it asks the host for more over two channels, and Sailfish OS provides neither:

- **rmtfs** serves the modem's EFS (the `modemst1`, `modemst2`, `fsg` and `fsc` partitions) over shared memory. Never let anything write those partitions: they hold the IMEI and the per-unit calibration. Run rmtfs read-only on the partitions (`-r -P`; then everything the modem learns is lost at every boot and every GNSS start is a cold start), or on files seeded once from the partitions (`-o /var/lib/rmtfs`, a root-only directory; the Pro1-X way). With `-s` rmtfs also starts the modem remoteproc itself once it serves.
- **tqftpserv** answers the modem's TFTP requests over QRTR. It maps `/readonly/firmware/image/...` to the remoteproc's `firmware-name` directory under `/lib/firmware`. At every start the modem asks for its carrier configuration there, `modem_pr/mcfg/configs/mcfg_hw/mbn_hw.dig` and `mcfg_sw/mbn_sw.dig`. Missing, tqftpserv logs `unable to open ..., reject` and the modem runs on defaults: calls and data still work, GNSS tracks no satellite at all.

Both come from `github.com/linux-msm` (`rmtfs`, `tqftpserv`, and `qrtr` for `qrtr-lookup`); the Pro1-X port packages them as RPMs. Only these two daemons are still needed: the QRTR name service and the protection-domain mapper are in the kernel (`CONFIG_QRTR`, `CONFIG_QCOM_PD_MAPPER`). Guides that list four daemons are from 2021. If your firmware lives on the mounted partition rather than under `/lib/firmware`, check where your tqftpserv build looks; the Pro1-X keeps the modem firmware under `/lib/firmware`.

The MCFG files are on the `modem` partition, which Android mounts at `/vendor/firmware_mnt`. Mount it read-only and bind its `image/modem_pr` next to `modem.mbn` before tqftpserv starts, and wait for the partition link, because early in boot udev may not have created it yet:

**DEVICE**
```sh
fw=/lib/firmware/qcom/<soc>/<vendor>/<device>   # where firmware-name points
mnt=/run/<device>/modem
slot=$(sed -n 's/.*androidboot.slot_suffix=\(_[ab]\).*/\1/p' /proc/cmdline)
dev=/dev/disk/by-partlabel/modem${slot:-_a}
mkdir -p $mnt $fw/modem_pr
for i in $(seq 1 30); do [ -e $dev ] && break; sleep 1; done
mountpoint -q $mnt || mount -t vfat -o ro,nodev,nosuid,noexec $dev $mnt
mountpoint -q $fw/modem_pr || mount --bind $mnt/image/modem_pr $fw/modem_pr
```

The rmtfs unit from the sources has `ConditionPathExists=/dev/qcom_rmtfs_mem1`, which is not there yet when the unit is scheduled, so the service is silently skipped. Clear the condition and wait for the node instead:

```ini
[Unit]
ConditionPathExists=
StartLimitIntervalSec=0

[Service]
ExecStartPre=/bin/sh -c 'for i in $(seq 1 180); do [ -e /dev/qcom_rmtfs_mem1 ] && exit 0; sleep 1; done; exit 1'
ExecStart=
ExecStart=/usr/bin/rmtfs -o /var/lib/rmtfs -s
TimeoutStartSec=240
```

Two things follow from this chain:

- **Wi-Fi depends on it.** On WCN3990-class chips (ath10k_snoc) Wi-Fi only comes up after the modem remoteproc, rmtfs and tqftpserv run. Debug a missing wlan0 from the modem side first.
- **`/sys/class/remoteproc/*/recovery` is a safety switch.** With recovery enabled, a client that crashes a DSP in a loop can freeze the phone; disabled, a crash leaves that DSP and everything on it (audio on the ADSP) down until reboot. Choose per experiment and check the default first.

Tested on the Pro1-X: Sailfish OS 5.2.0.22, kernel 7.3, modem firmware MPSS.HA.1.1. Before the MCFG mount the modem heard no satellite; after it, satellites were tracked at the same spot and the modem kept its EFS across boots.

### Modem (Qualcomm, QRTR) (Pro1-X)

Calls, SMS and mobile data work on the Pro1-X (SM6115, modem firmware MPSS.HA.1.1) with Sailfish OS 5.2.0.22 on kernel 7.3: data with ping and HTTPS, calls both ways including from suspend, SMS both ways. The modem has no USB side; it talks QMI over QRTR, and the data path runs through the IPA. Each piece below is in `github.com/moWerk/sailfishos-pro1x-mainline` (`patches/ofono/`, `droid-config-qx1050/sparse/`).

#### Kernel and firmware

`CONFIG_QRTR=y`, `CONFIG_QRTR_SMD=y`, `CONFIG_QCOM_PD_MAPPER=y`, `CONFIG_QCOM_Q6V5_PAS=m` for the remoteproc, `CONFIG_QCOM_IPA=m` and an IPA node in the device tree: `rmnet_ipa0` only exists with it, and oFono builds its modem from that netdev. Then the rmtfs and tqftpserv chain from [Firmware](#the-modem-and-the-dsps-fetch-files-at-runtime-qualcomm-pro1-x). `qrtr-lookup` lists the modem's services once it is up; `qmicli -d qrtr://0 --dms-get-ids` talks to it without any modem manager.

#### Sailfish's oFono has no QRTR, the Sailfish-patched upstream one has

Jolla's oFono 1.29 has `qmimodem` disabled and its `gobi` plugin speaks USB QMUX only. Upstream oFono 2.9 and later has `plugins/qrtrqmi.c`, and `github.com/sailfishos-native/ofono` is upstream 2.12 plus Jolla's Sailfish patches (the slot manager, the D-Bus interfaces lipstick and Settings talk to). The PinePhone port builds it as `ofono-2.12.0+git1` with `ell-0.70` in `nemo:devel:hw:pine:dontbeevil`, and that build runs on the Pro1-X with the three patches below on top.

oFono 2.x takes a deny-list, never the 1.x `--plugin` allow-list; `qrtrqmi`, `qmimodem`, the slot manager and provisioning are built in. `/var/lib/environment/ofono/<device>.conf`:

```sh
OFONO_ARGS=--noplugin=aml,phonesim
```

`aml` is Jolla's 1.29-era plugin left behind by sailfish-aml and fails to load against 2.x; `phonesim` wants a config file that does not exist.

oFono's provisioning service runs as the Android-era `radio` user. Create it in your kickstart `%post`:

```sh
getent group radio >/dev/null || groupadd -g 1001 radio
getent passwd radio >/dev/null || useradd radio --uid 1001 -g radio -r -s /sbin/nologin
```

#### oFono sees the SIM, Settings doesn't

`org.ofono.Manager.GetModems` lists the modem online, `SimManager` says present, the network is registered, and still Settings shows no SIM, connman has no cellular technology and the status bar no signal. Settings, connman, the status bar and commhistoryd read `org.nemomobile.ofono.ModemManager`, the Sailfish slot manager, which lists only modems a plugin registers as slots (`ofono_slot_driver_register`). Jolla's patch set adds that to `gobi` only.

The Pro1-X port adds a slot driver to `plugins/qrtrqmi.c`, modelled on sailfishos-native's patch `0010-Add-slot-support-to-the-gobi-plugin`: register the driver in probe, add one slot when DMS GET_IDS answers (the IMEI, or a placeholder), follow the SIM atom through `ofono_watch` so an absent SIM ends up ABSENT instead of leaving the manager unready forever. About 135 lines, `patches/ofono/0019-qrtrqmi-add-slot-driver.patch`, applies on 2.12 and 2.13. Check with:

**DEVICE**
```sh
dbus-send --system --print-reply --dest=org.ofono / org.nemomobile.ofono.ModemManager.GetAll
```

`AvailableModems` and `EnabledModems` must list your modem.

#### The SIM stays "detected", nothing registers

Some modem firmwares do not keep the UIM primary GW provisioning session. The card shows present, PIN state `not-initialized`, no registration ever. Activate the session before oFono, every boot, idempotently (postmarketOS's `msm-modem-uim-selection` does the same):

**DEVICE**
```sh
status=$(qmicli -d qrtr://0 --uim-get-card-status)
if echo "$status" | grep -q "Primary GW:.*session doesn't exist"; then
  aid=$(echo "$status" | awk '/Application type:.*usim/ { f = 1 } f && /Application ID:/ { getline; gsub(/[^0-9A-Fa-f]/, ""); print; exit }')
  qmicli -d qrtr://0 --uim-change-provisioning-session="session-type=primary-gw-provisioning,activate=yes,slot=1,aid=$aid"
fi
```

`qmicli` comes with `libqmi-utils` from native-common; libqmi 1.34 there is built with QRTR support.

#### Mobile data: "Failed to bind MUX"

oFono's `udevng` builds the `qrtrsoc` modem only when `rmnet_ipa0` and at least three `rmnet_dataN` links exist (`mux_id = N + 1`). On an IPA that offloads MAPv4 checksums (`/sys/class/net/rmnet_ipa0/device/feature/rx_offload` says `MAPv4`) the links need the matching flags, which iproute2 cannot set and qmicli 1.34 can:

**DEVICE**
```sh
ip link set rmnet_ipa0 up
for n in 0 1 2; do
  qmicli -d qrtr://0 --link-add="iface=rmnet_ipa0,prefix=rmnet_data,mux-id=$((n + 1)),flags=ingress-map-cksumv4|egress-map-cksumv4"
done
```

Then, before the first context, the modem must be told which IPA endpoints form the data port and which framing to use, and the bind must not carry the client-type TLV. Upstream oFono does none of this, so every context fails with `Failed to bind MUX` and connman's connect returns NotImplemented. The sequence, taken from ModemManager connecting on the same phone: DPM OPEN_PORT (service 47; the IPA endpoint ids from `/sys/class/net/rmnet_ipa0/device/modem/{tx,rx}_endpoint_id`, crossed over), WDA SET_DATA_FORMAT (raw-ip; QMAPv4 up and down when the IPA offloads MAPv4, plain QMAP otherwise; 32 datagrams of 32768 bytes down), WDS BIND_MUX_DATA_PORT without TLV 0x13. `patches/ofono/0020-qrtrqmi-ipa-data-path.patch`. Two traps: with plain QMAP on a MAPv4 IPA the context activates and uplink counts, but no downlink arrives; and the modem keeps the first data format it was given until it restarts, so test format changes on a fresh boot.

#### Incoming calls never ring

Two causes on the Pro1-X, both needed:

- oFono's QMI voice driver registers a handler for ALL CALL STATUS but never sends VOICE INDICATION REGISTER, and this modem (with its carrier configuration loaded) sends call status only to clients that registered. Outgoing calls worked; incoming ones reached the modem and never oFono. `patches/ofono/0021-qmimodem-voicecall-register-for-call-notifications.patch` sends the registration (TLV 0x13, call notification events) at probe.
- With its MCFG files the modem selects the SIM's carrier profile, here one with voice domain `ps-preferred`: the network routes incoming calls to VoLTE, which the port has no IMS stack for, and the caller hears "not available" until an outgoing call has made the phone register for circuit-switched voice. Set the voice domain to `cs-preferred` at every modem start (NAS SET SYSTEM SELECTION PREFERENCE; qmicli has no option for it, the port carries a small libqmi script, `qx1050-voice-domain.py`). Calls then ring awake and from suspend.

#### Don't let the modem hold the user session

`systemd-user-sessions` orders after `network.target`, and oFono is `WantedBy=network.target`. A preparation unit ordered before oFono therefore delays the whole UI until the modem is ready, here up to a minute (the modem restarts once at boot on this firmware). Mask `network.target.wants/ofono.service` in `/etc` and let the preparation unit start oFono when it is done:

```ini
[Unit]
After=rmtfs.service tqftpserv.service
Before=ofono.service

[Service]
Type=simple
ExecStart=/usr/bin/<device>-modem-prep.sh
```

The script waits for the UIM service in `qrtr-lookup`, activates the provisioning session, creates the links, sets the voice domain, then `systemctl --no-block start ofono.service`, and keeps watching: a modem restart drops the session and the links, and an oFono started on an unprepared modem hangs, with lipstick behind it. Make the preparation idempotent and either `RemainAfterExit=yes` or a watch loop, because a unit pulled in by `ofono.service` reruns on every oFono restart.

#### The Settings SIM page

Settings > Connectivity > SIM cards is `jolla-settings-networking-multisim`, which only Jolla's multi-SIM device patterns pull in. Add it to your adaptation pattern, and set `ModemCount` in `hw-settings.ini` (see [Sensors](#sensors)). The mobile data toggle works through connman as on any device.

Not done on the Pro1-X: VoLTE and IMS, a second SIM, IPv6, roaming. Call audio is in [Audio](#audio).

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

#### No IIO devices at all: the sensors are behind the DSP (Pro1-X)

On Qualcomm SoCs with a Sensor Core (SM6115 and relatives) the accelerometer, gyroscope, magnetometer, light and proximity sensors are not on a bus the kernel drives. They hang off the ADSP's Sensor Subsystem (SSC) and are reached over FastRPC and QMI; the only IIO devices are the PMIC's ADCs. What the Pro1-X runs instead, bottom up:

1. **hexagonrpcd** (`github.com/linux-msm/hexagonrpc`) serves the sensor registry and configuration to the ADSP from a directory tree. It needs the fork with write support and the sensor registry interface (pull request 27 plus two patches by nyymi609), the registry data from a working port of the same device (the NixOS port's `hexagonfs` tree here), a writable copy of the registry (the ADSP rewrites it), the platform name the ADSP asks for (`QRD` selects the configs for this board) and the `/dev/fastrpc-adsp` node, which appears late in boot. Kernel side: the fastrpc and SMP2P fixes from the Lenovo TB-J606L port (haltia-de), or the ADSP's sensor process crashes at the first resume from suspend.
2. **libssc** (Dylan Van Assche, `codeberg.org/DylanVanAssche/libssc`) is the client library; `ssccli --sensor accelerometer` is the first test. It needs libqmi-glib with QRTR, libqrtr-glib and protobuf-c, which Sailfish lacks (the port packages protobuf-c).
3. **sensorfw adaptors**, one plugin per sensor (`libssc<sensor>adaptor-qt5.so`), built against `sensorfw-qt5-devel`; no sensorfw fork needed. Mapped in `/etc/sensorfw/sensord.conf.d/60-<device>-ssc.conf` under `[plugins]`. Readings arrive already in the phone frame, so start with the identity matrix and verify by tilting; a mount matrix copied from an IIO-based stack inverted the axes here.

Hazards: a client that crashes the ADSP in a loop can freeze the phone when remoteproc recovery is on; with recovery off, a crash takes the ADSP and with it audio down until reboot. Supervise the first run. A client that blocks while the sensor service is missing blocks sensorfwd's main loop and with it lipstick's start (libssc 0.4.4 did; patched in the port).

Package sources and READMEs: `packages/hexagonrpc`, `packages/libssc`, `packages/protobuf-c`, `packages/sensorfw-qt5-ssc` in the Pro1-X port. Tested: Sailfish OS 5.2.0.22, kernel 7.3: auto-rotation, auto brightness, proximity in calls, compass, and the Sensor Core's step counter through its own adaptor.

#### Settings shows no sensor items, sensorfwd says "plugin not available"

sensorfwd enables a sensor only when its hardware feature is declared: `20-sensors-default.conf` `[available]` maps each sensor to a `Feature_*` flag that ssu-sysinfo reads from `/usr/share/csd/settings.d/hw-settings.ini`. Without that file only Suspend and Reboot exist. The same list gates Settings items (auto brightness needs `LightSensor=1`) and the status-bar battery indicator (`Battery=1`). A missing declaration looks exactly like a missing driver. This applies to IIO sensors too. From the Pro1-X file:

```ini
[features]
GSensor=1
Gyro=1
ECompass=1
LightSensor=1
ProxSensor=1
Battery=1
Hall=1

[LED]
LedType=RGB

[SIM]
ModemCount=1
```

#### Apps log "Could not connect to the <type> sensor backend" while sensorfwd works

QtSensors' sensorfw plugin registers its backends only from `/etc/xdg/QtProject/Sensors.conf` (`[Default]`, `QAccelerometer=sensorfw.accelerometer` and so on). droid-hal-configs ships it; a port that assembles its configuration another way has to.

### Power

#### The CPU keeps jumping to its highest frequency

We found two causes:

- **Real-time tasks:** by default every real-time task makes schedutil jump to max, and Sailfish runs some daemons as real-time (dsme). `kernel.sched_util_clamp_min_rt_default = 0` in a `/usr/lib/sysctl.d/` file cut the time at max frequency about 6x.
- **Kernel bug on 32-bit:** on 7.x kernels, an overflow in the scheduler's load tracking (from `089d84203ad4`) kept idle cores at max frequency. We fixed it by making `r` a `u64` in `update_cfs_rq_load_avg()`.

#### Unplugged, the USB controller stays on (Pro1-X)

The DWC3 core forbids runtime PM at probe until userspace allows it; Android's init writes `auto`, nothing in Sailfish does. Unplugged, the controller stays active and its PHY supplies stay on, also through system sleep. A udev rule at `bind` (probe resets the file, so `add` is too early):

```
ACTION=="bind", SUBSYSTEM=="platform", DRIVER=="dwc3", ATTR{power/control}="auto"
```

Sleep current on the Pro1-X went from 40-42 mA to 29-32 mA.

#### Nothing wakes the phone once it really sleeps (Pro1-X)

Once the SoC reaches a deep idle state, only interrupts that are wakeup sources wake it, and several that matter on a phone are not, by default, on mainline: the PMIC Type-C port (a cable plugged into a sleeping Pro1-X was seen minutes later), the modem's glink edge (an SMS waited for the next RTC alarm), the touch controller for double-tap to wake. These are kernel changes (`wakeup-source` on the nodes plus small driver patches) in the Pro1-X series. The lesson for a port: list what must wake your phone, then check each in `/sys/kernel/debug/wakeup_sources` before calling suspend done. The wake about every minute on the Pro1-X came from connman: without WoWLAN it reconnected after every resume, and its Wi-Fi autoscan hold-off is a wakeup timer (a one-line patch to `sailfish_wifi` in the port makes it a plain timeout; its effect is not measured yet).

### Encryption

#### A passphrase prompt appears on the console during boot

With home encryption, `systemd-ask-password-console` prints its prompt on the console and wipes the boot logo. The real unlock is done by Sailfish's unlock agent. We mask `systemd-ask-password-console.path` and `.service` (symlinks to `/dev/null` in `/etc/systemd/system/`).

On armv7, also set `CONFIG_CRYPTO_AES_ARM_BS=y` for NEON AES-XTS.

### Storage

#### Optional: more space for home

If the device has large partitions Sailfish doesn't use, like Android's `system` and `cache`, the pack script can add them to the `sailfish` volume group as extra physical volumes (one more sparse image per partition, flashed to that partition). On first boot the initrd gives all free space in the volume group to home. victara gains about 4 GB this way.

### Camera and video (Pro1-X)

Both cameras of the Pro1-X work in jolla-camera with Sailfish OS 5.2.0.22 on kernel 7.3: photos, autofocus, tap to focus, flash. 1080p30 H.264 video with AAC sound was proven through jolla-camera's own encodebin pipeline from the real camera, driven headless; recording from the app's UI was not exercised. The stack is CAMSS in the kernel, libcamera with its software ISP on the GPU, and Sailfish's own Qt 5.6 camerabin plugin with `libcamerasrc` inside it. Nothing from Android.

#### Kernel

The camera sensors, the lens driver and the CAMSS resources for the SoC are in the device kernel series (for SM6115 none of them is upstream yet). CAMSS registers its cameras only when every sensor of its async notifier has bound, so one sensor failing its probe hides every camera: on the Pro1-X the front sensor sometimes times out on I2C at boot, and a oneshot retries the bind. The flash LED joins the rear camera's media device as a V4L2 flash subdevice (`CONFIG_V4L2_FLASH_LED_CLASS=m`, `flash-leds` in the camera node): the camera app runs sandboxed with `/sys` read-only, but it may open `/dev/v4l-subdev*`, so the plugin drives the flash there.

#### libcamera

- Use 0.6 or newer. native-common's 0.4.0 asserts in `PipelineHandler::stop()` when a camera is released. The Pro1-X builds 0.7.2 with native-common's packaging.
- The software ISP runs on the GPU by default when EGL works; the stream size sets the frame rate (about 32 fps at 1280x960 here). Run the viewfinder in a binned sensor mode and switch to the full mode for the still.
- Out-of-tree sensors need a `CameraSensorHelper`, static properties and a tuning file, or the IPA refuses them.
- The session user needs the system dma-buf heap, or there are no cameras:

  ```
  SUBSYSTEM=="dma_heap", KERNEL=="system", GROUP="video", MODE="0660"
  KERNEL=="udmabuf", GROUP="video", MODE="0660"
  ```

- Autofocus: upstream libcamera had no software-ISP autofocus as of October 2026; the Pro1-X backports Milan Zamazal's RFC series plus fixes (a sharpness statistic, tap to focus through `AfWindows`, `AfState` out). Prove the lens by hand with `v4l2-ctl --set-ctrl focus_absolute` before blaming autofocus software.
- First test without any app: `cam -c 1 --capture=10 --file=/tmp/f#.ppm`.

#### jolla-camera

- Sailfish's camerabin takes its source from the session environment. Put it in `/var/lib/environment/nemo/70-<device>-camera.conf`, because apps start from boosters and never see shell exports:

  ```sh
  QT_GSTREAMER_CAMERABIN_VIDEOSRC=libcamerasrc
  GST_PLUGIN_FEATURE_RANK=avenc_aac:primary
  ```

  `VIDEOSRC`, not `SRC`: the latter replaces camerabin's whole source, is meant for droidcamsrc, and fails to link.
- Remove `nemo-qtmultimedia-plugins-gstvideotexturebackend` from the pattern. It is the hybris QML video node and makes QtMultimedia ask for `droideglsink`; without it Qt's own renderer backs `VideoOutput`.
- **Black viewfinder, "Resource not found" for a file that exists**: sailjail hides `/usr/share/libcamera`, so inside the sandbox the software ISP finds no IPA or tuning file and offers only raw Bayer. `whitelist /usr/share/libcamera` in `/etc/firejail/disable-common.local`.
- Sailfish's qtmultimedia (mer-5.6, the `+v4l` build from native-common) needs patches to work with libcamerasrc: it crashed on a source without size caps, listed every CAMSS video node as a camera, requested no size, failed the pipeline on one late frame, never read the initial ready-for-capture, ignored the camera orientation. The Pro1-X carries 14 patches in `patches/qtmultimedia/` (device enumeration through libcamera, sizes from the device provider, orientation and front-camera mirroring, flash, tap to focus, ISO, a small viewfinder with a full-size still).
- Resolutions come from dconf (`/etc/dconf/db/vendor.d/jolla-camera-hw.txt`): `imageResolution`, `viewfinderResolution*` and `videoResolution` per camera, and `/apps/jolla-camera/maxVideoResolution='1920x1080'` to cap video at what the encoder does.
- Boosters keep the old libraries mapped: after replacing libcamera or qtmultimedia, restart `booster-silica-media@jolla-camera.service` and `booster-silica-media@jolla-camera-lockscreen.service` as the session user.

#### Video recording and hardware codecs

Jolla's `gst-plugins-good` is built with `-Dv4l2-probe=false`, which compiles out the registration of the stateful V4L2 codec elements (`v4l2h264enc`, `v4l2h264dec`, ...). Hybris devices encode through droidmedia and never needed them; a mainline port with a V4L2 codec (Qualcomm Venus) has no hardware encoder without them. Rebuild the package with the option on (one spec change, `patches/gst-plugins-good/`; the cost is that every `/dev/video*` is opened once when the registry is built). The audio track needs an AAC encoder: `avenc_aac` from gst-libav is registered with rank none and never chosen by encodebin, hence the `GST_PLUGIN_FEATURE_RANK` line above.

Venus wants a firmware the driver accepts: on SM6115 and QCM2290 the driver refuses anything below 6.0.55, and the vendor image on the Pro1-X was 6.0.49. linux-firmware's `qcom/venus-6.0/venus.mbn` loaded on this retail unit, so the board node needs no `firmware-name` for Venus. Decoding through `v4l2h264dec` is registered by the same build; playback through it was not tested on the Pro1-X.

A fallback camera app that talks to libcamera directly is piggz's `harbour-shutter`; as of October 2026 its OBS build does not resolve on Sailfish 5.x.

### Boot on an A/B Android bootloader (Pro1-X)

The Pro1-X boots a plain Android boot image (header v0, kernel with the DTB appended) straight from ABL, as chapter 6 produces one; no second bootloader. Two things victara's bootloader does not have:

- **The slot has to be marked successful after every boot.** ABL counts boot attempts per slot and stops booting a slot that is never marked. Run `qbootctl -m` (`github.com/linux-msm/qbootctl`, packaged in the Pro1-X port; it needs the 7.x `scsi_bsg_ufs.h` vendored, because the SDK's kernel headers predate it) from a oneshot in `multi-user.target`.
- **Partitions carry the slot suffix** (`boot_a`, `modem_a`). Read `androidboot.slot_suffix` from `/proc/cmdline` where a script needs them, as the firmware mount in [Firmware](#firmware) does.

This ABL refuses `fastboot boot`, so every kernel test is a flash of `boot_a`; keep the previous image. From a running system `systemctl reboot bootloader` lands in fastboot. The first logo after power-on is drawn by the signed firmware and cannot be changed; the port draws its own splash from the initramfs onto `/dev/fb0` as its first step, with the kernel console kept off the screen.

### Not solved yet

- **Calls, SMS and mobile data on Qualcomm modems** on victara: the modem talks QMI (it needs `rmtfs` and `qrtr`), and Jolla's ofono is built without its QMI driver. The Pro1-X has them working with the Sailfish-patched upstream oFono, see [Modem](#modem-qualcomm-qrtr-pro1-x); its three oFono patches are not upstream anywhere yet.
- **Camera** on victara. See [Camera and video](#camera-and-video-pro1-x) for the Pro1-X.
- **Hardware video decoding.** Encoding works on the Pro1-X through Venus (same section); decoding is untested there.


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
