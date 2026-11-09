%if 0%{!?rpm_device:1}
%define rpm_device %{device}
%endif

%if 0%{!?kernel_arch:1}
%define kernel_arch arm
%endif

# The prjconf should have an ExportFilter like this (mer/sailfish has this):
#   ExportFilter: \.armv7hl\.rpm$ armv8el
# We lie about our architecture and allows OBS to cross-publish this 486 cross-built spec to the armv7hl repos
%if 0%{?device_target_cpu:1}
%define _target_cpu %{device_target_cpu}
%else
%define _target_cpu %{_arch}
%endif

%if 0%{!?defconfig:1}
%define defconfig defconfig
%endif

%if 0%{!?kernel_config:1}
%define kernel_config %{defconfig}
%endif

%if 0%{!?kernel_image:1}
%define kernel_image Image.gz
%endif

%if 0%{!?source_directory:1}
%define source_directory linux
%endif

%if 0%{!?build_Image:1}
%define build_Image 1
%endif

%if 0%{!?build_uImage:1}
%define build_uImage 0
%endif

%if 0%{!?build_zImage:1}
%define build_zImage 0
%endif

%if 0%{!?build_modules:1}
%define build_modules 1
%endif

%if 0%{!?deviceinfo_flash_offset_second:1}
%define deviceinfo_flash_offset_second 0x00000000
%endif

%if 0%{!?deviceinfo_bootimg_append_vbmeta:1}
%define deviceinfo_bootimg_append_vbmeta false
%endif

%define _tmpdir tmp

Name:           kernel-adaptation-%{rpm_device}
Summary:        Kernel adaptation for %{rpm_device}
Group:          System
License:        GPLv2
Provides:       kernel
Provides:       droid-hal-kernel
%if 0%{?bootimg_name:1}
Provides:       droid-hal-img-boot
Provides:       droid-hal-img-recovery
%endif
Source:         %{name}-%{version}.tar.bz2
Requires(pre):  kmod >= 9
BuildRequires:  pkgconfig(ncurses)
BuildRequires:  pkgconfig(openssl)
BuildRequires:  kmod >= 9
BuildRequires:  flex
BuildRequires:  bison
BuildRequires:  bc
BuildRequires:  openssl
#!BuildIgnore:  rpmlint-mini
BuildRequires:  clang
BuildRequires:  lz4
%if 0%{?fedora}
BuildRequires:  python3
%else
BuildRequires:  python3-base
%endif

%if 0%{?crossbuild:1}
BuildRequires:  cross-%{crossbuild}-gcc
BuildRequires:  cross-%{crossbuild}-binutils
BuildRequires:  cross-%{crossbuild}-as
%endif

%description
%{summary}.

%prep
%setup -q -n %{name}-%{version}

%build

export ARCH=%{kernel_arch}
export LOCALVERSION="-%{device}"
export CLANG_TRIPLE=%{?clangtriple}
export CROSS_COMPILE=%{?crosscompile}
export CROSS_COMPILE32=%{?crosscompile32}
export HOSTLDFLAGS=%{?hostldflags}

# Distro hardening flags from %%set_build_flags break kbuild
unset CFLAGS CXXFLAGS LDFLAGS

rm -rf %{_tmpdir}
mkdir -p %{_tmpdir}

%if 0%{?crossbuild:1}
export CROSS_COMPILE=%{crossbuild}-meego-linux-gnu-
# FIXME? Shouldn't be needed
export PATH=/opt/cross/bin/:$PATH
%endif

%if 0%{?extra_config:1}
[ -e "./%{extra_config}" ] && cp -f ./%{extra_config} %{source_directory}/arch/%{kernel_arch}/configs/%{extra_config}
%endif

%if 0%{?apply_patches:1}
echo "***** Applying patches *****"
sh patches/apply-patches.sh %{source_directory}
%endif

%if 0%{?build_Image:1}
echo "***** Building Image *****"
make %{?makeopts} -C %{source_directory} -j$(nproc) O=out %{?kcflags} %{kernel_config}
make %{?makeopts} -C %{source_directory} -j$(nproc) O=out %{?kcflags}
%endif

%if 0%{?build_modules:1}
echo "***** Building Modules *****"
make %{?makeopts} -C %{source_directory} -j$(nproc) O=out %{?kcflags} modules
%endif

%if 0%{?build_dtboimage:1}
echo "***** Building DTBO Image *****"
DTBOS=""
for dtbo in %{deviceinfo_dtbo}; do
    DTBOS+=" %{source_directory}/out/arch/%{kernel_arch}/boot/dts/$dtbo"
done
python2 kernel-adaptation-simplified/mkdtboimg.py create %{source_directory}/out/dtbo.img $DTBOS
%endif

%install
echo "***** Installing *****"

export ARCH=%{kernel_arch}
export LOCALVERSION="-%{device}"
export CLANG_TRIPLE=%{?clangtriple}
export CROSS_COMPILE=%{?crosscompile}
export CROSS_COMPILE32=%{?crosscompile32}
export HOSTLDFLAGS=%{?hostldflags}

# Distro hardening flags from %%set_build_flags break kbuild
unset CFLAGS CXXFLAGS LDFLAGS

# Fall back to the tools bundled with kernel-adaptation-simplified when the
# android build tools are not provided by the environment
export MKBOOTIMG_PATH="${MKBOOTIMG_PATH:-./kernel-adaptation-simplified}"
if [ -n "${AVB_PATH:-}" ]; then
    AVBTOOL="python3 $AVB_PATH/avbtool.py"
else
    AVBTOOL="python2 ./kernel-adaptation-simplified/avbtool"
fi

%if %{build_modules}
mkdir -p %{buildroot}/lib/modules
make -C %{source_directory} O=out %{?makeopts} INSTALL_MOD_STRIP=1 modules_install INSTALL_MOD_PATH=%{buildroot}
%endif

echo "***** Building Boot Image *****"
KERNEL=%{source_directory}/out/arch/%{kernel_arch}/boot/%{kernel_image}
%if 0%{?deviceinfo_bootimg_prebuilt_dtb:1}
DTB="$PWD/%{deviceinfo_bootimg_prebuilt_dtb}"
%else
DTB=%{source_directory}/out/arch/%{kernel_arch}/boot/dts/%{deviceinfo_dtb}
%endif
%if "%{?deviceinfo_append_dtb}" == "true"
cat "$KERNEL" "$DTB" > %{_tmpdir}/kernel-dtb
KERNEL="$PWD/%{_tmpdir}/kernel-dtb"
%endif
EXTRA_ARGS=""
OUT=%{source_directory}/out/arch/%{kernel_arch}/boot/boot.img

# Boot ramdisk: an explicit ramdisk macro or a halium-boot-ramdisk.img in the
# device repo wins, otherwise the unified halium ramdisk fetched by
# setup-env.sh is used, with ramdisk-overlay appended when present
%if 0%{?ramdisk_directory:1}
(cd %{ramdisk_directory} && find . | cpio -o -H newc -R 0:0 | gzip -9) > %{_tmpdir}/ramdisk-directory.img
RAMDISK="$PWD/%{_tmpdir}/ramdisk-directory.img"
%else
RAMDISK="%{?ramdisk}"
%endif
if [ -z "$RAMDISK" ]; then
    if [ -f ./halium-boot-ramdisk.img ]; then
        RAMDISK="$PWD/halium-boot-ramdisk.img"
    else
        RAMDISK="$HALIUM_RAMDISK"
    fi
    if [ ! -f "$RAMDISK" ]; then
        echo "No boot ramdisk found, run setup-env.sh or provide one"
        exit 1
    fi
    case "%{?deviceinfo_ramdisk_compression}" in
        lz4) COMPRESSION_CMD="lz4 -l -9" ;;
        *)   COMPRESSION_CMD="gzip -9" ;;
    esac
    if [ "%{?deviceinfo_ramdisk_compression}" = "lz4" ]; then
        gzip -dc "$RAMDISK" | $COMPRESSION_CMD > %{_tmpdir}/boot-ramdisk.img
        RAMDISK="$PWD/%{_tmpdir}/boot-ramdisk.img"
    fi
    if [ -d ./ramdisk-overlay ]; then
        if [ "$RAMDISK" != "$PWD/%{_tmpdir}/boot-ramdisk.img" ]; then
            cp "$RAMDISK" %{_tmpdir}/boot-ramdisk.img
            RAMDISK="$PWD/%{_tmpdir}/boot-ramdisk.img"
        fi
        (cd ramdisk-overlay && find . | cpio -o -H newc | $COMPRESSION_CMD) >> "$RAMDISK"
    fi
fi

mkdir -p %{buildroot}/boot

if [ "%{deviceinfo_bootimg_header_version}" -le 2 ]; then
    EXTRA_ARGS+=" --base %{deviceinfo_flash_offset_base} --kernel_offset %{deviceinfo_flash_offset_kernel} --ramdisk_offset %{deviceinfo_flash_offset_ramdisk} --second_offset %{deviceinfo_flash_offset_second} --tags_offset %{deviceinfo_flash_offset_tags} --pagesize %{deviceinfo_flash_pagesize}"
fi

if [ "%{deviceinfo_bootimg_header_version}" -eq 2 ]; then
    EXTRA_ARGS+=" --dtb $DTB --dtb_offset %{deviceinfo_flash_offset_dtb}"
fi

# Create boot.img
if [ "%{deviceinfo_bootimg_header_version}" -le 2 ]; then
    python3 $MKBOOTIMG_PATH/mkbootimg.py --kernel "$KERNEL" --ramdisk "$RAMDISK" --cmdline "%{deviceinfo_kernel_cmdline}" --header_version %{deviceinfo_bootimg_header_version} -o "$OUT" --os_version %{deviceinfo_bootimg_os_version} --os_patch_level %{deviceinfo_bootimg_os_patch_level} $EXTRA_ARGS
else
%if "%{?deviceinfo_bootimg_has_init_boot_partition}" == "true"
    # Kernel-only boot.img, the ramdisk goes into init_boot.img
    python3 $MKBOOTIMG_PATH/mkbootimg.py --kernel "$KERNEL" --header_version %{deviceinfo_bootimg_header_version} -o "$OUT" --os_version %{deviceinfo_bootimg_os_version} --os_patch_level %{deviceinfo_bootimg_os_patch_level} $EXTRA_ARGS

    INIT_BOOT=%{source_directory}/out/arch/%{kernel_arch}/boot/init_boot.img
    python3 $MKBOOTIMG_PATH/mkbootimg.py --ramdisk "$RAMDISK" --header_version %{deviceinfo_bootimg_header_version} -o "$INIT_BOOT"
%else
    python3 $MKBOOTIMG_PATH/mkbootimg.py --kernel "$KERNEL" --ramdisk "$RAMDISK" --header_version %{deviceinfo_bootimg_header_version} -o "$OUT" --os_version %{deviceinfo_bootimg_os_version} --os_patch_level %{deviceinfo_bootimg_os_patch_level} $EXTRA_ARGS
%endif

    # Build vendor_boot
    %if 0%{?build_vendor_boot:1}
        export DEVICEINFO_KERNEL_CMDLINE="%{deviceinfo_kernel_cmdline}"
        export DEVICEINFO_DTB=%{?deviceinfo_dtb}
        export DEVICEINFO_RAMDISK_COMPRESSION=%{deviceinfo_ramdisk_compression}
        export DEVICEINFO_FLASH_PAGESIZE=%{deviceinfo_flash_pagesize}
        export DEVICEINFO_FLASH_OFFSET_BASE=%{deviceinfo_flash_offset_base}
        export DEVICEINFO_FLASH_OFFSET_KERNEL=%{deviceinfo_flash_offset_kernel}
        export DEVICEINFO_FLASH_OFFSET_RAMDISK=%{deviceinfo_flash_offset_ramdisk}
        export DEVICEINFO_FLASH_OFFSET_TAGS=%{deviceinfo_flash_offset_tags}
        export DEVICEINFO_FLASH_OFFSET_DTB=%{deviceinfo_flash_offset_dtb}
        export DEVICEINFO_BOOTIMG_QCDT=%{deviceinfo_bootimg_qcdt}
        export DEVICEINFO_BOOTIMG_HEADER_VERSION=%{deviceinfo_bootimg_header_version}
        export DEVICEINFO_BOOTIMG_PARTITION_SIZE=%{deviceinfo_bootimg_partition_size}
        export DEVICEINFO_BOOTIMG_OS_VERSION=%{deviceinfo_bootimg_os_version}
        export DEVICEINFO_BOOTIMG_OS_PATCH_LEVEL=%{deviceinfo_bootimg_os_patch_level}
        export DEVICEINFO_ROOTFS_IMAGE_SECTOR_SIZE=%{deviceinfo_rootfs_image_sector_size}
        export DEVICEINFO_HALIUM_VERSION=%{deviceinfo_halium_version}
        export DEVICEINFO_KERNEL_DISABLE_MODULES=%{deviceinfo_kernel_disable_modules}
        export INSTALL_MOD_PATH=%{buildroot}/lib/modules/

        bash ./kernel-adaptation-simplified/mkvendorboot.sh "$DTB"
        cp tmp/vendor-ramdisk/vendor_boot.img %{buildroot}/boot/ || true
    %endif
fi

#Use AVB Tool to create final boot.img
if [ -n "%{?deviceinfo_bootimg_partition_size}" ]; then
    EXTRA_ARGS=""
    [ -f "./rsa4096_boot.pem" ] && EXTRA_ARGS=" --key ./rsa4096_boot.pem --algorithm SHA256_RSA4096"
    $AVBTOOL add_hash_footer --image "$OUT" --partition_name boot --partition_size %{deviceinfo_bootimg_partition_size} $EXTRA_ARGS

    if [ -n "%{deviceinfo_bootimg_append_vbmeta}" ] && %{deviceinfo_bootimg_append_vbmeta}; then
        $AVBTOOL append_vbmeta_image --image "$OUT" --partition_size "%{deviceinfo_bootimg_partition_size}" --vbmeta_image ./vbmeta.img
    fi
fi

%if "%{?deviceinfo_bootimg_has_init_boot_partition}" == "true"
$AVBTOOL add_hash_footer --image "$INIT_BOOT" --partition_name init_boot --partition_size %{?deviceinfo_init_boot_partition_size}%{!?deviceinfo_init_boot_partition_size:8388608}
cp "$INIT_BOOT" %{buildroot}/boot/
%endif

%if %{build_Image}
echo "***** Installing Image *****"
cp %{source_directory}/out/arch/%{kernel_arch}/boot/*Image* %{buildroot}/boot/
%if 0%{?deviceinfo_dtb:1}
cp %{source_directory}/out/arch/%{kernel_arch}/boot/dts/%{deviceinfo_dtb} %{buildroot}/boot/ || true
%endif
cp %{source_directory}/out/arch/%{kernel_arch}/boot/boot.img %{buildroot}/boot/%{?bootimg_name} || true
%endif

%if 0%{?devicetrees:1}
for dtb in %{devicetrees}; do
mkdir -p %{buildroot}/boot/$(basename $(dirname $dtb))
cp %{source_directory}/out/arch/%{kernel_arch}/boot/dts/$dtb %{buildroot}/boot/$dtb
cp %{buildroot}/boot/*/*.dtb %{buildroot}/boot/
done
%endif

%if 0%{?build_dtboimage:1}
cp %{source_directory}/out/dtbo.img %{buildroot}/boot/ || true
%endif

# Build filelist
find %{buildroot} -type f -empty -delete
find %{buildroot} ! \( -type d \) | sed -e s,"%{buildroot}",,g> %{_tmpdir}/kernel-adaptation.files

%post
# This oneshot is enabled only during package upgrades, as initial
# installation is done when we flash device.
if [ $1 -ne 1 ] ; then
  add-preinit-oneshot /var/lib/platform-updates/flash-bootimg.sh || :
fi

%files -f %{_tmpdir}/kernel-adaptation.files
