console.log("Starting test_ipc.js");
if (process.ipcSocket) {
    console.log("IPC socket found!");
    process.ipcSocket.on('data', function(data) {
        console.log("Received IPC message:", data.toString());
        process.ipcSocket.write("ECHO: " + data.toString());
    });
} else {
    console.log("No IPC socket!");
}
