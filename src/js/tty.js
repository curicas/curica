var net = require('net');
var os = require('os');
var stream = require('stream');

function isatty(fd) {
    return os.isatty(fd);
}

function ReadStream(fd) {
    var sock = net._createSocketFromFd(fd);
    if (sock.unref) sock.unref();
    sock.isTTY = isatty(fd);
    sock.isRaw = false;
    sock.setRawMode = function(mode) {
        this.isRaw = !!mode;
        var os_mod = require('os');
        if (os_mod.setRawMode) {
            os_mod.setRawMode(fd, this.isRaw);
        }
        return this;
    };
    return sock;
}

function WriteStream(fd) {
    var sock = net._createSocketFromFd(fd);
    if (sock.unref) sock.unref();
    sock.isTTY = isatty(fd);
    sock.columns = 80;
    sock.rows = 24;
    return sock;
}

module.exports = {
    isatty: isatty,
    ReadStream: ReadStream,
    WriteStream: WriteStream
};
