# CheckRoom_CubieboardServer
Checkroom Project - Server(OS : Cubian, TCP / UART)

- This app use openCV & mjpeg-streamer.
- This source have some paths hard coded like "/dev/ttyS1", "/home/cubie/mjpg-streamer/" and etc...
- If some motion is occured in sight of webcam, it will capture webcam, and notificate to android app. - TCP
- It transmis sensor infomation to android app from arduino periodically. - TCP/UART
- When it gets specific user command, it will stream webcam to webview in android app(mjpeg-streamer)
