
## Install

Install https://github.com/mysensors/Raspberry.git

Compile this source

## Usage 
```
Usage: sudo ./openmilight [hdfslun:p:q:r:c:b:k:v:w:]

   -h                       Show this help
   -d                       Show debug output
   -f                       Fade mode
   -s                       Strobe mode
   -l                       Listening (receiving) mode
   -u                       UDP mode
   -n NN<dec>               Resends of the same message
   -p PP<hex>               Prefix value (Disco Mode)
   -1 RRRR<hex>             Code of remote for bridge 1 (Port 8891)
   -2 RRRR<hex>             Code of remote for bridge 2 (Port 8892)
   -3 RRRR<hex>             Code of remote for bridge 3 (Port 8893)
   -4 RRRR<hex>             Code of remote for bridge 4 (Port 8894)   
   -c CC<hex>               Color byte
   -b BB<hex>               Brightness byte
   -k KK<hex>               Key byte
   -v SS<hex>               Sequence byte
   -w SSPPRRRRCCBBKKNN<hex> Complete message to send
   
   For milight bridge emulation (replace ABCD with actual remote hex code, if simulating a real bridge/remote you can get the hex code by running openmilight -l (listen) and viewing the returned hex codes:
   ./openmilight -m -1 ABCD -2 ABCD -3 ABCD -4 ABCD

 Author: Roy Bakker (2015)
 Modifications for multiple milight bridges: Matthew Wire (2016)

 Inspired by sources from: - https://github.com/henryk/
                           - http://torsten-traenkner.de/wissen/smarthome/openmilight.php
```
