var EventEmitter = function() {
    this._events = {};
};


EventEmitter.prototype.on = function(eventName, listener) {
    if (!this._events[eventName]) {
        this._events[eventName] = [];
    }
    this._events[eventName].push(listener);
    return this;
};

EventEmitter.prototype.once = function(eventName, listener) {
    var self = this;
    var onceWrapper = function(a, b, c, d) {
        var args = [];
        if (a != undefined) args.push(a);
        if (b != undefined) args.push(b);
        if (c != undefined) args.push(c);
        if (d != undefined) args.push(d);
        self.removeListener(eventName, onceWrapper);
        listener.apply(null, args);
    };
    onceWrapper.originalListener = listener;
    return this.on(eventName, onceWrapper);
};

EventEmitter.prototype.emit = function(eventName, a, b, c, d) {
    if (!this._events[eventName]) return false;
    var args = [];
    if (a != undefined) args.push(a);
    if (b != undefined) args.push(b);
    if (c != undefined) args.push(c);
    if (d != undefined) args.push(d);
    var listeners = this._events[eventName].slice(0);
    for (var i = 0; i < listeners.length; i = i + 1) {
        listeners[i].apply(null, args);
    }
    return true;
};

EventEmitter.prototype.removeListener = function(eventName, listener) {
    if (!this._events[eventName]) return this;
    var listeners = this._events[eventName];
    for (var i = 0; i < listeners.length; i = i + 1) {
        if (listeners[i] == listener || listeners[i].originalListener == listener) {
            listeners.splice(i, 1);
            break;
        }
    }
    return this;
};

EventEmitter.prototype.off = function(eventName, listener) {
    return this.removeListener(eventName, listener);
};

module.exports = EventEmitter;
