#mm
sudo adb disable-verity
sudo adb remount

sudo adb push ../../out/target/product/hikey/system/bin/optee_example_hello_world /system/bin/

