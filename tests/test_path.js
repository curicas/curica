const path = require('path');

if (path.join('a', 'b', 'c') == 'a/b/c') {
    console.log('path.join ok');
} else {
    console.log('path.join failed');
}

if (path.basename('a/b/c.txt') == 'c.txt') {
    console.log('path.basename ok');
} else {
    console.log('path.basename failed');
}
