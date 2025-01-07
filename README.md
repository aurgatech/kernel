# Kernel Build Instructions
This guide provides step-by-step instructions to build the Linux kernel for an ARM target using a Linaro toolchain.

## Prerequisites
- A Linux-based system (e.g., Ubuntu) or WSL2.
- Basic knowledge of the command line.
- Sufficient disk space (at least 10 GB recommended).

## Steps to Build the Kernel
### Create a kernel Folder
Create a directory to organize the kernel build files:
```bash
mkdir kernel
cd kernel
```
### Download the Toolchain
Download the Linaro ARM toolchain:
```bash
wget https://snapshots.linaro.org/gnu-toolchain/11.3-2022.06-1/arm-linux-gnueabihf/gcc-linaro-11.3.1-2022.06-x86_64_arm-linux-gnueabihf.tar.xz
```
Extract the downloaded toolchain:
```bash
tar xvf gcc-linaro-11.3.1-2022.06-x86_64_arm-linux-gnueabihf.tar.xz
```
### Clone the Kernel Repository
Clone the kernel source code repository:
```bash
git clone https://github.com/aurgatech/aurga-viewer-v1-kernel-5.16.20.git linux-5.16.20
```
### Set Up the Environment
Navigate to the kernel repository and set up the toolchain path:
```bash
cd linux-5.16.20
export PATH=$(pwd)/../gcc-linaro-11.3.1-2022.06-x86_64_arm-linux-gnueabihf/bin:$PATH
```

### Configure the Kernel
Run the menu-based configuration tool:
```bash
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- aurga_viewer_bcmdhd_defconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- menuconfig
```
### Build the Kernel
Compile the kernel and modules.
```bash
make -j$(nproc) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- mfloat-abi=hard uImage modules LOADADDR=0x40008000
```
### Build Device Tree Blobs
Compile the device tree blobs (DTBs):
```bash
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- dtbs
```
## Output Files
After a successful build, the following files will be generated:
- Kernel Image: `arch/arm/boot/uImage`
- Device Tree Blobs: Located in `arch/arm/boot/dts/`.

## Notes
- Ensure you have the required dependencies installed (e.g., libncurses-dev, bc, bison, flex, etc.).
- If you encounter errors, check the logs and ensure the toolchain path is correctly set.
- For faster builds, use the -j option with the number of CPU cores available on your system