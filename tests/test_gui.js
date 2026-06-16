var GUI = require('gui');
console.log('GUI module loaded');

var win = new GUI.Window({
    title: "Curica Native Window Test",
    width: 600,
    height: 400,
    html: "<h2>Hello from Curica GUI!</h2><curica-button class='suggested'>Click Me</curica-button>"
});

console.log('Window instance created');

// Keep process alive slightly for the window to spawn
setTimeout(function() {
    console.log('Done wait.');
}, 5000);
