# Screenshot

Screenshot tool for LingmoOS.

## Dependencies

Arch / Manjaro Dependencies:

```shell
sudo pacman -S cmake ninja qt6-base qt6-declarative qt6-tools libxcb
```

Screen recording also needs `ffmpeg` (with libx264) and, to record audio, a
PulseAudio compatible server (`pipewire-pulse` or `pulseaudio`) plus `pactl`
(`libpulse`).

Debian / Ubuntu Dependencies:
```shell
sudo apt install cmake qtbase5-dev qtdeclarative5-dev qtquickcontrols2-5-dev qttools5-dev qttools5-dev-tools qml-module-qtquick-controls2 qml-module-qtquick2 qml-module-qtquick-layouts qml-module-qt-labs-platform qml-module-qt-labs-settings qml-module-qtqml qml-module-qtquick-window2 qml-module-qtquick-shapes qml-module-qtquick-dialogs qml-module-qtquick-particles2
```

## Screen recording

Choose *Video* in the capture toolbar, pick the screen, an area or a window,
and press *Record*. Recordings are saved as H.264 MP4 files in
`~/Videos/Screen Recordings` (localized folder name).

While recording, a small indicator with the elapsed time is shown (and a tray
icon when a system tray is available). To stop the recording, click *Stop* or
run:

```shell
lingmo-screenshot --stop-recording
```

which calls `StopRecording` on the `com.lingmo.ScreenRecorder` D-Bus service
(`/Recorder`), so it can be bound to a global shortcut.

## Build

```shell
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr ..
make
sudo make install
```

## License

This project has been licensed by GPLv3.
