---- BBTether UDEV rules ------------------------------------------------------
BBTether / Thibaut Colar http://wiki.colar.net/bbtether

On Systems using UDEV (most recent linux), the blackberry will usually get set to
be owned by root with permissions of 600 or 644, preventing a regular user to
write to the usb device.
This requires bbtether to be run as root.

As an alternative we can tell udev to set the device permissions, such as a
regular user can read / write to it.

** Installing the rules **
I provide two rules, the first one (99-bbtether.rules) will make the blackberry
read/writable by anyone (666), this is easy but could be a security risk if you
have multiple users on your machine (if you care)

The second one (99-bbtether_user.rules) will set the permissions such as only
your user (and root) can access the device (660), this is safer but you will need
to edit 99-bbtether_user.rules and replace johnd by your actual user name.

So you do EITHER
---- Read/ Write for everybody -------------------
sudo cp udev/99-bbtether.rules /etc/udev/rules.d/
---- OR OR OR permissions for your user only -----
vi 99-bbtether_user.rules     (update the 'owner' name)
sudo cp udev/99-bbtether_user.rules /etc/udev/rules.d/
--------------------------------------------------

** Have UDEV pick-up the changes **
This imight be done automatically, but to be sure:
---
sudo udevcontrol reload_rules
---
then unplug and replug the blackberry.

You can check it worked like this:
---
ls /dev/usbdev* -lai
---
And should now see the correct owner/permissions.

You can now run bbtether as a regular user.