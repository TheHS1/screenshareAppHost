# Remote Desktop App
This is this host side of a remote desktop application developed using Visual Studio 2022 for Windows. It allows users to host their computer to allow connections from the [client program](https://github.com/TheHS1/screenshareAppClient). The application uses p2p UDP networking for establishing a connection, and therefore requires a signaling server accessible by both computers found in the server folder.

Desktop Duplication API code Licensed under MS-LPL

## Goals
* Secure control of computer over the internet
* Design Windows host and multi-platform client
* Designed such that other OS hosts could be written relatively easily
* Low latency experience 
* Encrypt traffic for security
* Data streaming efficient

## Features
* H264 encoded stream
* UDP p2p networking
* Reliable frame delivery
* AES-128 Encryption
* Average 35ms latency (excluding network latency)

## Building the Application
1. **Open the Solution**
   Launch Visual Studio 2022 and open the solution file (.sln)
2. **Set Build Configuration** Choose the appropriate build configuration (Debug or Release)
3. **Build the Project** Build the solution to compile the source code and generate the executable.

## Running the Application
Execute the exe file located in .\x64\\{Release/Debug}\DesktopDuplication.exe

## Technical Details

### High Level Overview
1. Interact with win32 API to grab raw Direct2D texture
2. Encode frame data using H264
3. The encoder required specific Direct2D texture format, intermediate step of converting texture format
4. Encrypt H264 encoded data using AES-128 encryption algorithm
6. Use sockets to send encrypted data over the network
7. Unencrypt, Decode and parse H264 data into frames
8. Display the frames
9. Capture client mouse and keyboard events and send them back to the host via sockets
