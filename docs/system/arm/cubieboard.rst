Cubietech Cubieboard (``cubieboard``)
=====================================

The ``cubieboard`` model emulates the Cubietech Cubieboard,
which is a Cortex-A8 based single-board computer using
the AllWinner A10 SoC.

Emulated devices:

- Timer
- UART
- RTC
- EMAC
- SDHCI
- USB controller
- SATA controller
- TWI (I2C)

Boot options
""""""""""""

The Cubeiboard machine can start using the standard -kernel functionality
for loading a Linux kernel or ELF executable. Additionally, the Cubieboard
machine can also emulate the BootROM which is present on an actual Allwinner A10
based SoC, which loads the bootloader from a SD card, specified via the -sd argument
to qemu-system-arm.

Running mainline Linux
""""""""""""""""""""""

Mainline Linux kernels from 4.19 up to latest master are known to work.
To build a Linux mainline kernel that can be booted by the Orange Pi PC machine,
simply configure the kernel using the sunxi_defconfig configuration:

.. code-block:: bash

  $ ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- make mrproper
  $ ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- make sunxi_defconfig

Build the Linux kernel with:

.. code-block:: bash

  $ ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- make

To boot the newly built linux kernel in QEMU with the Cubieboard machine, use:

.. code-block:: bash

  $ qemu-system-arm -M cubieboard -nic user -nographic \
      -kernel /path/to/linux/arch/arm/boot/zImage \
      -append 'console=ttyS0,115200' \
      -dtb /path/to/linux/arch/arm/boot/dts/sun4i-a10-cubieboard.dtb

Cubieboard images
"""""""""""""""""""

Note that the mainline kernel does not have a root filesystem. You may provide it
with an Armbian image for Cubieboard which can be downloaded from:

   https://www.armbian.com/cubieboard/

When using an image as an SD card, it must be resized to a power of two. This can be
done with the ``qemu-img`` command. It is recommended to only increase the image size
instead of shrinking it to a power of two, to avoid loss of data. For example,
to prepare a downloaded Armbian image, first extract it and then increase
its size to two gigabytes as follows:

.. code-block:: bash

  $ qemu-img resize Armbian_21.08.1_Cubieboard_focal_current_5.10.60.img 2G

To boot using the Armbian image on SD card, simply add the -sd
argument and provide the proper root= kernel parameter:

.. code-block:: bash

  $ qemu-system-arm -M cubieboard -nic user -nographic \
      -kernel /path/to/linux/arch/arm/boot/zImage \
      -append 'console=ttyS0,115200 root=/dev/mmcblk0p2' \
      -dtb /path/to/linux/arch/arm/boot/dts/sun4i-a10-cubieboard.dtb \
      -sd Armbian_21.08.1_Cubieboard_focal_current_5.10.60.img

Instead of providing a custom Linux kernel via the -kernel command you may also
choose to let the Cubieboard machine load the bootloader from SD card, just like
a real board would do using the BootROM. Simply pass the selected image via the -sd
argument and remove the -kernel, -append, -dbt and -initrd arguments:

.. code-block:: bash

  $ qemu-system-arm -M cubieboard -nic user -nographic \
       -sd Armbian_21.08.1_Cubieboard_focal_current_5.10.60.img

Note that both the official Armbian images start a lot of userland programs via
systemd. Depending on the host hardware and OS, they may be slow to emulate.
To help improve performance, you can give the following kernel parameters via
U-Boot (or via -append):

.. code-block:: bash

  => setenv extraargs 'systemd.default_timeout_start_sec=9000 loglevel=7'

Running U-Boot
""""""""""""""

U-Boot mainline can be build and configured using the cubieboard_defconfig
using similar commands as describe above for Linux. Note that it is recommended
for development/testing to select the following configuration setting in U-Boot:

  Device Tree Control > Provider for DTB for DT Control > Embedded DTB

To start U-Boot using the Cubieboard machine, provide the u-boot binary to
the -kernel argument:

.. code-block:: bash

  $ qemu-system-arm -M cubieboard -nic user -nographic \
      -kernel /path/to/uboot/u-boot -sd disk.img

Use the following U-boot commands to load and boot a Linux kernel from SD card:

.. code-block:: bash

  => setenv bootargs console=ttyS0,115200
  => load mmc 0 0x42000000 zImage
  => load mmc 0 0x43000000 sun4i-a10-cubieboard.dtb
  => bootz 0x42000000 - 0x43000000

Cubieboard integration tests
""""""""""""""""""""""""""""""

The Cubieboard machine has several integration tests included.
To run the whole set of tests, build QEMU from source and simply
provide the following command:

.. code-block:: bash

  $ AVOCADO_ALLOW_LARGE_STORAGE=yes avocado --show=app,console run \
     -t machine:cubieboard tests/avocado/boot_linux_console.py
