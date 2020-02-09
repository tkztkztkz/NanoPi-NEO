#!/bin/sh

make clean
make DISPTYPE=-DDISPLAY_NANOHATOLED TARGET=mpd_gui.cpp.o_nanohatoled

make clean
make DISPTYPE=-DDISPLAY_13IPS240240 TARGET=mpd_gui.cpp.o_ips240240

make clean
make DISPTYPE="-DDISPLAY_IPS320240 -DFEATURE_INA219 -DGPIO_5BUTTON" TARGET=mpd_gui.cpp.o_naspidac

make clean
make DISPTYPE="-DDISPLAY_IPS320240 -DGPIO_5BUTTON" TARGET=mpd_gui.cpp.o_naspidac_novoldet

make clean
make DISPTYPE="-DDISPLAY_IPS320240V -DGPIO_5BUTTON -DGPIO_BUTTON_ROTATE" TARGET=mpd_gui.cpp.o_naspidac_v_novoldet

make clean
make DISPTYPE="-DDISPLAY_IPS320240V -DFEATURE_INA219 -DGPIO_5BUTTON -DGPIO_BUTTON_ROTATE" TARGET=mpd_gui.cpp.o_naspidac_v


