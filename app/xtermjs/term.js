import Terminal from 'xterm';
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
// there is no public interface to disable linkifying
term.linkifier._linkMatchers = [];
term.open(document.getElementById('terminal'), true);
term.on('focus', function() {
    webkit.messageHandlers.focus.postMessage('focus');
});
term.on('resize', function(size) {
    webkit.messageHandlers.resize.postMessage('resize');
});

// copied from the fit addon, but without subtracting 17 pixels for a nonexistent scrollbar
function fit() {
    // find the size of the box
    const {
        width: parentWidth,
        height: parentHeight,
        paddingTop,
        paddingBottom,
        paddingLeft,
        paddingRight,
    } = window.getComputedStyle(term.element.parentElement);
    const boxWidth = parseFloat(parentWidth) - parseFloat(paddingLeft) - parseFloat(paddingRight);
    const boxHeight = parseFloat(parentHeight) - parseFloat(paddingTop) - parseFloat(paddingBottom);
    
    // find the size of a character
    const subjectRow = term.rowContainer.firstElementChild;
    const origContent = subjectRow.innerHTML;
    subjectRow.style.display = 'inline';
    subjectRow.innerText = 'W';
    const charWidth = subjectRow.getBoundingClientRect().width;
    subjectRow.style.display = '';
    const charHeight = subjectRow.getBoundingClientRect().height;
    subjectRow.innerHTML = origContent;
    
    term.resize(Math.floor(boxWidth / charWidth),
                Math.floor(boxHeight / charHeight));
}

fit();
window.addEventListener('resize', function() {
    fit();
});

const props = ['applicationCursor'];
const oldPropValues = {};
function termWrite(data) {
    term.write(data);
    for (const prop of props) {
        if (oldPropValues[prop] != term[prop]) {
            oldPropValues[prop] = term[prop];
            webkit.messageHandlers.propUpdate.postMessage([prop, term[prop]]);
        }
    }
}
window.termWrite = termWrite;

function updateStyle({foregroundColor, backgroundColor, fontSize}) {
    const style = `
    .terminal {
        font-size: ${fontSize}px;
        color: ${foregroundColor};
    }
    .terminal.focus:not(.xterm-cursor-style-underline):not(.xterm-cursor-style-bar) .terminal-cursor {
        background-color: ${foregroundColor};
        color: ${backgroundColor};
    }
    .terminal:not(.focus) .terminal-cursor {
        outline-color: ${foregroundColor};
    }
    `;
    document.getElementById('style').textContent = style;
    fit();
    fit(); // have to do a second time for it to correctly detect available space initially
}
window.updateStyle = updateStyle;

// allow touches to scroll the div that exists to display a scrollbar
term.element.addEventListener('touchstart', function(event) {
    event.stopPropagation();
}, {capture: true});
term.element.addEventListener('touchmove', function(event) {
    event.stopPropagation();
}, {capture: true});
