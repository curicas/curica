var EventEmitter = require('events');
var _dgram = require('_dgram');

function Socket(type) {
    EventEmitter.call(this);
    this.type = type;
    this._handle = _dgram.createSocket(type);
    
    var self = this;
    
    this._handle.on('message', function(buffer, rinfo) {
        self.emit('message', buffer, rinfo);
    });
    
    this._handle.on('error', function(err) {
        self.emit('error', err);
    });
    
    this._handle.on('close', function() {
        self.emit('close');
    });
}

Socket.prototype = new EventEmitter();
Socket.prototype.constructor = Socket;

Socket.prototype.bind = function(port, address, callback) {
    if (typeof address === 'function') {
        callback = address;
        address = undefined;
    }
    
    if (callback) {
        this.once('listening', callback);
    }
    
    var res = this._handle.bind(port, address);
    if (res) {
        var self = this;
        // Schedule listening event for the next tick
        setTimeout(function() {
            self.emit('listening');
        }, 0);
    }
    return this;
};

Socket.prototype.send = function(msg, offset, length, port, address, callback) {
    // Handling different overload signatures
    if (typeof offset === 'number' && typeof length === 'number') {
        // signature: send(msg, offset, length, port, address, [callback])
        if (typeof address === 'function') {
            callback = address;
            address = undefined;
        }
    } else {
        // signature: send(msg, port, address, [callback])
        callback = address;
        address = port;
        port = offset;
        offset = 0;
        length = msg.length;
    }
    
    var res = this._handle.send(msg, offset, length, port, address);
    if (callback) {
        var err = res ? null : new Error("Send failed");
        setTimeout(function() {
            callback(err);
        }, 0);
    }
    return this;
};

Socket.prototype.close = function(callback) {
    if (callback) {
        this.once('close', callback);
    }
    this._handle.close();
    return this;
};

Socket.prototype.address = function() {
    return this._handle.address();
};

function createSocket(type, listener) {
    var socket = new Socket(type);
    if (listener) {
        socket.on('message', listener);
    }
    return socket;
}

module.exports = {
    Socket: Socket,
    createSocket: createSocket
};
