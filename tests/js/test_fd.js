const net = require('net'); console.log(net._createSocketFromFd); const s = net._createSocketFromFd(0); console.log(s);
