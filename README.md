# EchoVR-Haptics

# To build
open "EchoVR-Haptics.sln", change from debug to release and set as x64, rename dll made to dbgcore.dll.

# To use
copy dbgcore.dll and haptics_config.txt over to where echo vr is located, it will look like "ready-at-dawn-echo-arena\bin\win10"
adjust the strength of haptics by changing value in haptics_config.txt from 0.0 up to 5.0, 5.0 is max.

This also allows a user to change FOV without needing 3rd party apps, can make it easier for recording, default value is 0.0 but can go to 2.0

# P.S
massive thanks to [@NotBlue](https://github.com/NotBlue-Dev) and his [tool](https://github.com/NotBlue-Dev/Echo-VR-Haptics), it gave me the starting grounds for what i needed and saved me lots of time.
