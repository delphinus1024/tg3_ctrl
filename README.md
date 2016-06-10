# tg3_ctrl (very alpha)

Olympus TG-3 controller.

## Description

This is a sample program to control Olympus TG-3 via Wi-fi.
Although I don't own any another Olympus cameras, it could be used with another Olympus OI Share ready cameras too. (need slight modification?)
NOTE: This software is at very alpha stage so that source codes remain untidy. In addition, behavior of the software is quite unsteady.

## Features

- Liveview
- Focusing by point clicking
- Shoot

## Requirement

- OpenCV 3.0.0 or above is preferable.
- Boost C++ 1.60 or above, using thread and filesystem
- Winsock2
- C++11
- Checked with win7&10 32bit + msys2 + MinGW gcc 5.3.0

## Usage

- Important! Disable firewall of your PC.
- Turn on the Wi-fi of TG3 with private connection mode.
- Return to your PC, find the Wi-fi AP that TG3 provides, login with password that TG3 shows.
- To see whether connection is done, ping 192.168.0.10
- To launch the software, ./tg3_ctrl.exe
- Then a liveview window appears. Mode is set to "A" (aperture priority)
- Click any point of the window to focus on it.
- Hit return key to shoot.
- Hit ESC to exit.

## Installation

1. Modify Makefile according to your host program Boost and OpenCV environment.
2. make

## Author

delphinus1024

## License

[MIT](https://raw.githubusercontent.com/delphinus1024/tg3_ctrl/master/LICENSE.txt)

