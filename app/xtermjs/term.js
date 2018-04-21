import Terminal from 'xterm';
import 'xterm/lib/addons/fit/fit';
import 'xterm/dist/xterm.css';
import './term.css';

// monkey patching so xterm doesn't need a textarea
Terminal.prototype.focus = function() {
    this.textarea.dispatchEvent(new FocusEvent('focus'));
};
Terminal.prototype.blur = function() {
    this.textarea.dispatchEvent(new FocusEvent('blur'));
};

window.term = new Terminal();
term.open(document.getElementById('terminal'), true);
term.on('focus', function() {
    webkit.messageHandlers.focus.postMessage('focus');
});
term.on('resize', function(size) {
    webkit.messageHandlers.resize.postMessage(size.cols + 'x' + size.rows);
});
term.fit();
window.addEventListener('resize', function() {
    term.fit();
});

// allow touches to scroll the div that exists to display a scrollbar
term.element.addEventListener('touchstart', function(event) {
    event.stopPropagation();
}, {capture: true});
term.element.addEventListener('touchmove', function(event) {
    event.stopPropagation();
}, {capture: true});
