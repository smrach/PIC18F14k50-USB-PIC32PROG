echo off
set pathC18=D:\Microchip\MCC18\bin\
set MCHP_PATH=D:\Microchip\
set MCHP_USB_PATH=D:\Microchip\USB\
set c18options=-D__DEBUG -mL -Ou- -Ot- -Ob- -Op- -Or- -Od+ -Opa- -k --verbose -w2
set piccpu=18F14K50
set builName=release\%piccpu%_PIC32PROG
set linkoptions=/u_CRUNTIME /u_DEBUG /z__MPLAB_BUILD=1 /z__MPLAB_DEBUG=1 /o"%builName%.cof" /M"%builName%.map" /W
set include18=-p=%piccpu% /i"%MCHP_PATH%MCC18\h" -I"%MCHP_PATH%Include"
set c18=%pathC18%mcc18.exe
set linker=%pathC18%mplink.exe
set lister=%pathC18%mp2cod.exe
echo on
echo CLEANING old files..
del objs\*.o
del release\%piccpu%_PIC32PROG.* /Q
echo COMPILING...
%C18% %include18% "usb_descriptors.c" -fo="objs\usb_descriptors.o" %c18options%
%C18% %include18% "%MCHP_USB_PATH%\usb_device.c" -fo="objs\usb_device.o" %c18options%
%C18% %include18% "%MCHP_USB_PATH%\HID Device Driver\usb_function_hid.c" -fo="objs\usb_function_hid.o" %c18options%
%C18% %include18% "pic32prog.c" -fo="objs\pic32prog.o" %c18options%
%C18% %include18% "main.c" -fo="objs\main.o" %c18options%
%linker% /p%piccpu% /l"..\..\C18\lib" "HID%piccpu%.lkr" "objs\usb_descriptors.o" "objs\usb_device.o" "objs\usb_function_hid.o" "objs\pic32prog.o" "objs\main.o" %linkoptions%
%lister% /p %piccpu% "%builName%.cof"
pause
