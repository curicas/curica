var EventEmitter = require('events');

var Readable = function() {
    EventEmitter.call(this);
    this.readable = true;
};
Readable.prototype = new EventEmitter();
Readable.prototype.constructor = Readable;

Readable.prototype.push = function(chunk) {
    if (chunk == null) {
        this.emit('end');
        this.readable = false;
    } else {
        this.emit('data', chunk);
    }
};

Readable.prototype.pipe = function(destination) {
    this.on('data', function(chunk) {
        destination.write(chunk);
    });
    this.on('end', function() {
        destination.end();
    });
    return destination;
};

var Writable = function() {
    EventEmitter.call(this);
    this.writable = true;
};
Writable.prototype = new EventEmitter();
Writable.prototype.constructor = Writable;

Writable.prototype.write = function(chunk) {
    if (this._write) {
        this._write(chunk, function() {});
    }
    return true;
};

Writable.prototype.end = function(chunk) {
    if (chunk != undefined) {
        this.write(chunk);
    }
    this.writable = false;
    this.emit('finish');
};

var Transform = function() {
    EventEmitter.call(this);
    this.readable = true;
    this.writable = true;
};
Transform.prototype = new EventEmitter();
Transform.prototype.constructor = Transform;

Transform.prototype.write = function(chunk) {
    if (this._transform) {
        this._transform(chunk, function() {});
    } else {
        this.push(chunk);
    }
    return true;
};

Transform.prototype.push = function(chunk) {
    if (chunk == null) {
        this.emit('end');
        this.readable = false;
    } else {
        this.emit('data', chunk);
    }
};

Transform.prototype.end = function(chunk) {
    if (chunk != undefined) {
        this.write(chunk);
    }
    this.writable = false;
    this.emit('finish');
    this.push(null);
};

Transform.prototype.pipe = function(destination) {
    this.on('data', function(chunk) {
        destination.write(chunk);
    });
    this.on('end', function() {
        destination.end();
    });
    return destination;
};

module.exports = {
    Readable: Readable,
    Writable: Writable,
    Transform: Transform
};
