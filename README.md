# linc_accesskit

hxcpp bindings for [AccessKit](https://github.com/AccessKit/accesskit-c)'s C library, for screen readers, as simple
uses it; apps don't call it themselves. Android only for now.

`accesskit.AccessKit` wraps the externs in `accesskit.Native` with ordinary static functions, so cppia scripts can call
them through a host. `linc/linc_accesskit.cpp` holds what has to stay native: Android calls it on its UI thread, where
Haxe objects can't be touched. It needs linc_sdl3 for SDL's JNI handle and to wake the main loop.

Run `haxe --run Fetch android` from this folder to put the library in `vendor/`.

AccessKit is under the MIT or Apache 2.0 licence.
