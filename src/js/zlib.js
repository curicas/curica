var _zlib = require('_zlib');

function deflate(buffer, callback) {
    if (!callback) throw new Error("Callback is required");
    _zlib.deflate(buffer, callback);
}

function inflate(buffer, callback) {
    if (!callback) throw new Error("Callback is required");
    _zlib.inflate(buffer, callback);
}

module.exports = {
    deflate: deflate,
    inflate: inflate
};
