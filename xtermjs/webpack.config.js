const path = require('path');
const webpack = require('webpack');

module.exports = {
    entry: './term.js',
    output: {
        filename: 'bundle.js',
        path: __dirname,
    },
    module: {
        rules: [
            {
                test: /\.js$/,
                exclude: /node_modules/,
                loader: 'babel-loader',
                options: {presets: ['env']},
            },
            // for xterm
            {
                test: /\.css$/,
                loader: 'style-loader!css-loader',
            },
        ],
    },
    plugins: [
        new webpack.IgnorePlugin(/.*\.js\.map$/i),
    ],
};
