const dgram = require('dgram');

const server = dgram.createSocket('udp4');
const client = dgram.createSocket('udp4');

server.on('error', (err) => {
  console.log(`server error:\n${err.stack}`);
  server.close();
});

server.on('message', (msg, rinfo) => {
  console.log(`server got: ${msg} from ${rinfo.address}:${rinfo.port}`);
  
  // echo back
  server.send(Buffer.from('Hello from server'), rinfo.port, rinfo.address, (err) => {
    if (err) console.log("server send error:", err);
  });
});

server.on('listening', () => {
  const address = server.address();
  console.log(`server listening ${address.address}:${address.port}`);
  
  // start client
  client.send(Buffer.from('Hello from client'), address.port, '127.0.0.1', (err) => {
    if (err) console.log("client send error:", err);
  });
});

client.on('message', (msg, rinfo) => {
  console.log(`client got: ${msg} from ${rinfo.address}:${rinfo.port}`);
  server.close();
  client.close();
  console.log("Success! Both sockets closed.");
});

server.bind(41234);
