#!/usr/bin/env bash
set -euo pipefail

user_name="${1:-pi}"
sudo groupadd --force spi
sudo usermod -aG spi "$user_name"
sudo usermod -aG dialout "$user_name"
sudo tee /etc/udev/rules.d/99-facephys-spi-gpio.rules >/dev/null <<'RULES'
# spidev access for the FacePhys display service; no world-writable device.
KERNEL=="spidev*", GROUP="spi", MODE="0660"
# libgpiod needs read/write access to the GPIO character device.
SUBSYSTEM=="gpio", KERNEL=="gpiochip*", GROUP="spi", MODE="0660"
# The OpenMV USB CDC camera is not UVC; expose a stable path to its serial
# JPEG stream while retaining the normal dialout ownership.
SUBSYSTEM=="tty", ATTRS{idVendor}=="1209", ATTRS{idProduct}=="abd1", GROUP="dialout", MODE="0660", SYMLINK+="openmv"
RULES
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=spidev
sudo udevadm trigger --subsystem-match=gpio
sudo udevadm trigger --subsystem-match=tty
echo "Re-login $user_name before using SPI/GPIO/OpenMV serial."
