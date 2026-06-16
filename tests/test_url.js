console.log('--- Testing URL ---');

const myURL = new URL('https://example.com:8080/foo/bar?q=123#hash');
console.log('href:', myURL.toString());
console.log('protocol:', myURL.protocol);
console.log('hostname:', myURL.hostname);
console.log('port:', myURL.port);
console.log('pathname:', myURL.pathname);
console.log('search:', myURL.search);
console.log('hash:', myURL.hash);

if (myURL.hostname != 'example.com') throw new Error('Invalid hostname');
if (myURL.port != '8080') throw new Error('Invalid port');
if (myURL.pathname != '/foo/bar') throw new Error('Invalid pathname');

myURL.searchParams.append('test', 'value');
console.log('Updated search:', myURL.searchParams.toString());

if (!myURL.searchParams.has('test')) throw new Error('SearchParams missing added value');

console.log('URL tests passed!');
