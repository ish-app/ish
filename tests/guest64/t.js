const crypto = require('crypto');
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
let s = 0;
for (let i = 0; i < 3e6; i++) s += Math.sqrt(i) * (i % 7);
console.log('node', process.arch, process.version.split('.')[0]);
console.log('fib(27) =', fib(27), 'sum =', s.toFixed(3));
console.log('sha256:', crypto.createHash('sha256').update('ish64').digest('hex').slice(0, 16));
const a = Array.from({length: 200000}, (_, i) => (i * 7919) % 10007).sort((x, y) => x - y);
console.log('sorted:', a[0], a[a.length - 1], JSON.stringify({x: [1, 2.5, null], y: 'z'}));
console.log('regexp:', 'The quick brown fox'.replace(/(\w+) (\w+)/, '$2 $1'), (0.1 + 0.2).toString(), 2n ** 100n);
const m = new Map(); for (let i = 0; i < 1e5; i++) m.set('k' + (i % 977), i);
console.log('map:', m.size, m.get('k5'));
Promise.resolve(42).then(v => new Promise(r => setTimeout(() => r(v + 1), 30))).then(v => console.log('async:', v));
