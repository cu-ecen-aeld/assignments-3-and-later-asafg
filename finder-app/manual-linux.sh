#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

OUTDIR="$(realpath "${OUTDIR}")"

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}
    # TODO: Add your kernel build steps here

    # apply patch to remove 'yyloc' error
    git apply ${FINDER_APP_DIR}/0001-scripts-dtc-Remove-redundant-YYLOC-global-declaratio.patch
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j12 all
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs 
fi

echo "Adding the Image in outdir"
cp -v ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir "${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
mkdir bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir usr/bin usr/lib usr/sbin var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
else
    cd busybox
fi

# TODO: Make and install busybox
make distclean
make defconfig
make -j12 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} 
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} CONFIG_PREFIX=${OUTDIR}/rootfs install

cd "${OUTDIR}/rootfs"
echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
lib_ld_linux="$(${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter" | awk -F / '{print $3}' | sed 's/]//g')"
libs="$(${CROSS_COMPILE}readelf -a bin/busybox | grep  "Shared library" | awk -F [ '{print $2}' | sed 's/]//g')"
lib_path="$(find "$(dirname "$(which aarch64-none-linux-gnu-gcc)")/.." -name ${lib_ld_linux})"
cp -v ${lib_path} lib
for lib_file in $libs;
do
  lib_path="$(find "$(dirname "$(which aarch64-none-linux-gnu-gcc)")/.." -name ${lib_file})"
  cp -v ${lib_path} lib64
done

# TODO: Make device nodes
echo "Creating device nodes"
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 1 5

# TODO: Clean and build the writer utility
echo "Compiling writer utility"
cd "${FINDER_APP_DIR}"
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} clean 
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} 

# TODO: Copy the finder related scripts and executables to the /home directory
cp -v writer "${OUTDIR}/rootfs/home"
cp -v finder.sh finder-test.sh "${OUTDIR}/rootfs/home"
mkdir "${OUTDIR}/rootfs/home/conf"
cp -v conf/{assignment.txt,username.txt} "${OUTDIR}/rootfs/home/conf"
# ../conf -> conf
sed -i 's#../conf#conf#g' "${OUTDIR}/rootfs/home/finder-test.sh"
cp -v autorun-qemu.sh "${OUTDIR}/rootfs/home"

# on the target rootfs

# TODO: Chown the root directory
sudo chown root:root "${OUTDIR}/rootfs"

# TODO: Create initramfs.cpio.gz
echo "Creating cpio archive"
cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root | sudo tee "${OUTDIR}/initramfs.cpio" > /dev/null
cd "${OUTDIR}"
sudo gzip -f initramfs.cpio

