var EventEmitter = require('events');

function Interface(options) {
    EventEmitter.call(this);
    this.input = options.input;
    this.output = options.output;
    this._prompt = '> ';
    this.buffer = '';
    
    var self = this;
    if (this.input) {
        this.input.on('data', function(chunk) {
            var chunkStr = chunk.toString();
            self.buffer += chunkStr;
            var lines = self.buffer.split('\n');
            // Remove carriage returns
            for (var i = 0; i < lines.length; i++) {
                if (lines[i].length > 0 && lines[i].charAt(lines[i].length - 1) === '\r') {
                    lines[i] = lines[i].slice(0, lines[i].length - 1);
                }
            }
            self.buffer = lines.pop(); // keep remainder
            
            for (var i = 0; i < lines.length; i++) {
                self.emit('line', lines[i]);
            }
        });
        this.input.on('end', function() {
            if (self.buffer.length > 0) {
                self.emit('line', self.buffer);
                self.buffer = '';
            }
            self.emit('close');
        });
    }
}

Interface.prototype = new EventEmitter();
Interface.prototype.constructor = Interface;

Interface.prototype.setPrompt = function(prompt) {
    this._prompt = prompt;
};

Interface.prototype.prompt = function(preserveCursor) {
    if (this.output) {
        this.output.write(this._prompt);
    }
};

Interface.prototype.question = function(query, cb) {
    if (this.output) {
        this.output.write(query);
    }
    var self = this;
    var onLine = function(line) {
        self.removeListener('line', onLine);
        cb(line);
    };
    this.on('line', onLine);
};

Interface.prototype.close = function() {
    if (this.input && typeof this.input.destroy === 'function') {
        this.input.destroy();
    }
    this.emit('close');
};

function createInterface(options) {
    return new Interface(options);
}

module.exports = {
    Interface: Interface,
    createInterface: createInterface
};
