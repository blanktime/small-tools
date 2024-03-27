@echo off
setlocal EnableDelayedExpansion
echo adb root
adb devices
adb devices > devices.txt
set number=1
echo ===========Device serial number===========
for /f "skip=1" %%i in (devices.txt) do (
	echo !number!. %%i
	set /a number+=1
)
echo ==========================================
set /p line=Please select the device serial number:

set num=1
for /f "skip=1" %%i in (devices.txt) do (
	if !num!==%line% set serial=%%i
	set /a num+=1
)

del devices.txt       
adb -s %serial% root

adb -s %serial% shell "mkdir /data/test"
adb -s %serial% shell "rm -rf /data/test/*"

echo push
adb -s %serial% push io_test /data/test/

adb -s %serial% shell "chmod 777 /data/test/*"
echo The files have been transferred

echo running sh...
echo ===========================================

set "version=1.0"
echo ============  version: %version% ================

adb -s %serial% shell "cd /data/test;./io_test 50000"
	
pause
