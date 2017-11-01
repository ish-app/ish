import Terminal from 'xterm';
Terminal.loadAddon('fit');
import 'xterm/dist/xterm.css';
import './term.css';

const term = new Terminal();
term.open(document.getElementById('terminal'))
term.fit();

term.on('data', function(data) {
    console.log(data);
    window.webkit.messageHandlers.sendInput.postMessage(data);
});

window.output = function(data) {
    term.write(data);
};
