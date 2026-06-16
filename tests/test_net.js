// test_net.js — Verifies TCP echo server and client via the net module
const net = require('net');

var received = '';

var server = net.createServer(function(socket) {
    socket.on('data', function(chunk) {
        socket.write(chunk);
        socket.end();
    });
});

server.listen(0, function() {
    var addr = server.address();
    var port = addr.port;
    console.log("Server listening on port:", port);

    var client = net.connect(port, '127.0.0.1', function() {
        console.log("Client connected");
        var buf = Buffer.from('hellonet');
        client.write(buf);
    });

    client.on('data', function(chunk) {
        received = chunk.toString();
        console.log("Echo received:", received);
    });

    client.on('close', function() {
        console.log("Client socket closed");
        server.close();
        console.log("net test complete. echo match:", received == 'hellonet');
        if (received == 'hellonet') {
            console.log("All net tests passed!");
        }
    });
});
