# GunCon 2 USB Lightgun Driver
Linux driver for the GunCon 2 light gun.

- A **joystick** device:
  - Reports absolute `ABS_X` and `ABS_Y` positions from the GunCon 2 sensor.
  - Reports trigger and other buttons as standard gamepad buttons (e.g. trigger as `BTN_LEFT` by default).
- A **mouse** device:
  - Provides relative motion and a standard mouse left-click, mapped from the same GunCon 2 trigger.

Emulators and frontends can use either:
- The joystick device (absolute aiming + buttons), or
- The mouse device (relative pointer + left click),
depending on what works best.

## Calibration

The GunCon 2 must be calibrated for your display.

The joystick device’s `ABS_X` and `ABS_Y` ranges can be adjusted using `evdev-joystick`. This allows you to map the raw GunCon 2 coordinates to the visible area of your screen.

Example (adjust the values to your setup):

```sh
# X axis (joystick device)
evdev-joystick --e /dev/input/by-id/usb-0b9a_016a-event-joystick -m 175 -M 720 -a 0

# Y axis (joystick device)
evdev-joystick --e /dev/input/by-id/usb-0b9a_016a-event-joystick -m 20 -M 240 -a 1
```
It is also included a simple script for calibrating the GunCon 2, however the calibration must be performed each time the GunCon 2 is connected. This can be done with a set of udev rules. 

For example;
```ini
SUBSYSTEM=="input", ATTRS{idVendor}=="0b9a", ATTRS{idProduct}=="016a", ACTION=="add", RUN+="/bin/bash -c 'evdev-joystick --e %E{DEVNAME} -m 175 -M 720 -a 0; evdev-joystick --e %E{DEVNAME} -m 20 -M 240 -a 1'"
```

### Automatic DKMS driver install and removal
```sh
./dkms_gcon2.sh all
./dkms_gcon2.sh remove
```

### Build and install

```sh
make modules
sudo make modules_install
sudo depmod -a
sudo modprobe guncon2
```

To reload after compiling you will first need to unload it using `sudo modprobe -r guncon2`.

