This is an OSX driver for the USB 3.0 express card (ECUSB3S11) from startech.com

The specs are here :
http://www.startech.com/Cards-Adapters/USB-3.0/Cards/1-Port-Flush-Mount-ExpressCard-SuperSpeed-USB-3-Card-Adapter~ECUSB3S1

## It's based on GenericUSBXHCI USB 3.0 driver first from original author Zenith432 (http://sourceforge.net/p/genericusbxhci/code/ci/master/tree/), and after by RehabMan (https://github.com/RehabMan/OS-X-Generic-USB3)

A rewrite is ongoing, to cleanup code by only targeting Yosemite for now and Renesas chipset functionality used in the startech express card.

A custom installer will also be added as described in Apple's documentation : https://developer.apple.com/library/mac/documentation/Darwin/Conceptual/KEXTConcept/KEXTConceptPackaging/packaging_tutorial.html

### How to Install (until an installer is provided) :

Install GenericUSBXHCI.kext using Kext Wizard or your favorite kext installer.

If you were previously using PXHCD.kext, you should probably remove it.

```
rm -rf /System/Library/Extensions/PXHCD.kext
````


### Downloads:

Downloads are available on Bitbucket:

https://bitbucket.org/RehabMan/os-x-generic-usb3/downloads

Archived (old) builds are available on Google Code:

https://code.google.com/p/os-x-generic-usb3/downloads/list


### Build Environment

My build environment is currently Xcode 6.1, using SDK 10.8, targeting OS X 10.7.

No other build environment is supported.


### 32-bit Builds

Currently, builds are provided only for 64-bit systems.  32-bit/64-bit FAT binaries are not provided.  But you can build your own should you need them.  I do not test 32-bit, and there may be times when the repo is broken with respect to 32-bit builds, but I do check on major releases to see if the build still works for 32-bit.

Here's how to build 32-bit (universal):

- xcode 4.61
- open GenericUSBXHCI.xcodeproj (do not change the SDK!)
- click on GenericUSBXHCI at the top of the project tree
- change Architectures to 'Standard (32/64-bit Intel)'

probably not necessary, but a good idea to check that the target doesn't have overrides:
- check/change Architectures to 'Standard (32/64-bit Intel)'
- build (either w/ menu or with make)

Or, if you have the command line tools installed, just run:

- For FAT binary (32-bit and 64-bit in one binary)
make BITS=3264

- For 32-bit only
make BITS=32


### Source Code:

The source code is maintained at the following sites:

https://bitbucket.org/RehabMan/os-x-generic-usb3

https://code.google.com/p/os-x-generic-usb3/

https://github.com/RehabMan/OS-X-Generic-USB3


### Feedback:

Please use the following thread on tonymacx86.com for feedback, questions, and help:

TODO: provide link


### Known issues:


### Change Log:

2014-10-16 (RehabMan)

- Merged with latest Zenith432 version

- Created new Universal build for compatibility with 10.7.5 through 10.10


2013-03-23 (RehabMan)

- Modified for single binary to work on ML, Lion (10.7.5 only)

- Optimize build to reduce code size and exported symbols.


2013-03-06 (Zenith432)

- Initial build provided by Zenith432 on insanelymac.com


### History

This repository contains a modified version of Zenith432's GenericUSBXHCI USB 3.0 driver.  All credits to Zenith432 for the original code and probably further enhancements/bug fixes.

Original sources came from this post on Insanely Mac:

http://www.insanelymac.com/forum/topic/286860-genericusbxhci-usb-30-driver-for-os-x-with-source/

Original repo:

http://sourceforge.net/p/genericusbxhci/code

My goal in creating this repository was just to create a single binary that could be used on 10.8.x, 10.7.5.  I simply optimized the build settings for a smaller binary, removed some of the #if conditionals and added runtime checks as appropriate for differences between versions.  Having a single optimized build for the Probook Installer makes the package smaller and easier to manage.

If you install my version on a 10.7.4 or prior, the driver will gracefully exit.
