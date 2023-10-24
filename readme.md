# Source Switcher
The application switches sources from RTSP to file and back and renders it on screen.

## Build
It uses cmake  
### Requirements:
1. gstreamer  
2. cmake >= 3.16

You should use GCC >= 11.4.0 (at least it was tested only on GCC 11.4.0)  

## How to run
Pass two arguments:
1. switch interval (sec)
2. file location

example:
SourceSwitcher 10 /home/user/Video/test.mp4