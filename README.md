Open Web Board Demo
==========
This repository contains some fixes and demo apps for [KDDI Open Web Board][owb].  
Note that current demos and patches are based on the 20150123 release.


Preparing Source Code
----------
Following the steps on [Official Documentation][owbsource] to prepare your source code for OWB.

Update Gaia
----------
Only certified apps are able to use the [OWB BLE API][owbble],  
the development process is the same as gaia apps.  
If you are installing a new certified app,  
put your app in `~/{YOUR_SOURCE_ROOT}/gaia/external-apps/`.  
You'll need to reset gaia for the first time.  
Note that this will erase user settings.

```sh
cd ~/{YOUR_SOURCE_ROOT}/gaia
make reset-gaia
```

If you're updating an existing app, just run:

```sh
cd ~/{YOUR_SOURCE_ROOT}/gaia
APP={YOUR_APP_NAME} make install-gaia
```

Debugging Gaia with WebIDE
----------
OWB comes with root privilege,  
so you don't need to worry about that.

Make sure to enable debugging for ADB and DevTools in Settings App.

To enable debugging on certified apps,  
use WebIDE to connect to the runtime,   
go to Runtime > Runtime Info,  
click "request higher privileges".

OWB will reboot,  
after that you can connect to the runtime again,  
and would be able to select any of your certified apps to debug.


Update Gecko
----------
If you have changes made in gecko,  
you can build gecko with following command:

```sh
cd ~/{YOUR_SOURCE_ROOT}
./rkst/mkimage.sh rk3066-eng -j1 gecko
```

and make sure to reboot OWB before updating gecko:

```sh
adb reboot
adb remount
adb shell stop b2g
cd ~/{YOUR_SOURCE_ROOT}/out/target/product/rk3066/
adb push ./system/b2g /system/b2g
adb reboot
```


Debugging Gecko
----------
In your cpp file, uncomment this line:

```c
#define __DEBUG__
```

and update gecko with the instructions in previous section.  
You'll be able to see your logs in adb logcat.


Tracing BT HCI Snoop Log
----------
To enable BT HCI Snoop Log,  
you need to modify /etc/bluetooth/bt_stack.conf

```sh
adb pull /etc/bluetooth/bt_stack.conf
```

and edit the file pulled out from your OWB with following line:
```sh
BtSnoopLogOutput=true
```

If you need to lower the log level for GATT, specify `TRC_GATT=5`.

Finally push it back to OWB and reboot:
```sh
adb remount
adb push bt_stack.conf /etc/bluetooth/bt_stack.conf
adb reboot
```

You can pull out the log when needed and analyze it with [wireshark][wireshark]:
```sh
adb pull /sdcard/btsnoop_hci.log
wireshark btsnoop_hci.log
```

Flash Images
----------
In case you messed up,  
you might need to know how to restore system  
with [official images][owbimage].  

Firstly, [download the images][owbimage].

On Windows, please make sure you install the [OWB driver][owbusbdriver] first,  
than [following the instructions to flash your OWB][flashwindows].

If you're using linux, download and use the [Rockchip Tool for Linux][flashlinux].


[owb]: http://opensource.kddi.com/owb/
[owbsource]: http://opensource.kddi.com/owb/owbsource.html
[owbusbdriver]: http://opensource.kddi.com/owb/owbusbdriver.html
[owbimage]: http://opensource.kddi.com/owb/owbimage.html
[owbble]: http://opensource.kddi.com/owb/owbble.html
[flashwindows]: http://opensource.kddi.com/owb/owbtool.html
[flashlinux]: http://www.hotmcu.com/wiki/Flashing_Firmware_Image_Files_Using_The_Rockchip_Tool#Linux
[wireshark]: https://www.wireshark.org/download.html
