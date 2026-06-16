var EventEmitter = require('events');
var stream = require('stream');
var Readable = stream.Readable;
var Writable = stream.Writable;

var ChildProcess = function(pid, stdinFd, stdoutFd, stderrFd) {
    EventEmitter.call(this);
    this.pid = pid;
    var self = this;

    // stdin is a Writable stream pointing to stdinFd
    this.stdin = new Writable();
    this.stdin._write = function(chunk, callback) {
        __child_process.__child_process_write(stdinFd, chunk, callback);
    };

    // stdout is a Readable stream coming from stdoutFd
    this.stdout = new Readable();
    __child_process.__child_process_read_start(stdoutFd, function(chunk) {
        if (chunk == null) {
            self.stdout.push(null);
        } else {
            self.stdout.push(chunk);
        }
    });

    // stderr is a Readable stream coming from stderrFd
    this.stderr = new Readable();
    __child_process.__child_process_read_start(stderrFd, function(chunk) {
        if (chunk == null) {
            self.stderr.push(null);
        } else {
            self.stderr.push(chunk);
        }
    });

    __child_process.__child_process_wait(this.pid, function(code) {
        self.emit('exit', code);
        self.emit('close', code);
    });
};
ChildProcess.prototype = new EventEmitter();
ChildProcess.prototype.constructor = ChildProcess;

function spawn(command, args, options) {
    var finalArgs = args;
    if (!finalArgs) finalArgs = [];
    var finalOptions = options;
    if (!finalOptions) finalOptions = {};
    
    var vfsArr = [];
    if (finalOptions.vfs) {
        var keys = Object.keys(finalOptions.vfs);
        for (var i = 0; i < keys.length; i = i + 1) {
            var key = keys[i];
            vfsArr.push(finalOptions.vfs[key] + '::' + key);
        }
    }
    
    var ret = __child_process.__child_process_spawn(command, finalArgs, vfsArr);
    return new ChildProcess(ret.pid, ret.stdinFd, ret.stdoutFd, ret.stderrFd);
}

module.exports = {
    spawn: spawn,
    ChildProcess: ChildProcess,
    execSync: __child_process.execSync
};
