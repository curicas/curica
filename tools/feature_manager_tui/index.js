var fs = require('fs');
var path = require('path');

var C = {
    reset: "\x1b[0m", cyan: "\x1b[36m", magenta: "\x1b[35m",
    green: "\x1b[32m", yellow: "\x1b[33m", red: "\x1b[31m",
    gray: "\x1b[90m", bgCyan: "\x1b[46;30m", bgGreen: "\x1b[42;30m",
    bold: "\x1b[1m", hide: "\x1b[?25l", show: "\x1b[?25h",
    clear: "\x1b[2J\x1b[H"
};

var FEATURES_DIR = path.join(__dirname, '../../references/development/features');
var PROPOSED_DIR = path.join(FEATURES_DIR, 'proposed');
var APPROVED_DIR = path.join(FEATURES_DIR, 'approved');
var COMPLETED_DIR = path.join(FEATURES_DIR, 'completed');

function ensureDir(dir) {
    if (!fs.existsSync(dir)) {
        try { fs.mkdirSync(dir); } catch(e) {}
    }
}

// Menu class to handle interactive prompts
function Menu(title, choices, width) {
    this.title = title;
    this.choices = choices;
    this.selected = 0;
    this.width = width || 60;
}

Menu.prototype.render = function() {
    var out = C.clear + C.bold + C.magenta + "=== " + this.title + " ===\n" + C.reset + "\n";
    out += "Use Arrow Keys to navigate, Enter to select.\n\n";
    
    // Draw box top
    out += C.cyan + "┌" + "─".repeat(this.width) + "┐\n" + C.reset;
    
    for (var i = 0; i < this.choices.length; i++) {
        var prefix = (i === this.selected) ? " > " : "   ";
        var line = prefix + this.choices[i].name;
        
        // Pad line to width
        var padLen = this.width - line.length;
        if (padLen < 0) {
            line = line.substring(0, this.width - 3) + "...";
            padLen = 0;
        }
        var padded = line + " ".repeat(padLen);
        
        if (i === this.selected) {
            out += C.cyan + "│" + C.bgCyan + padded + C.reset + C.cyan + "│\n" + C.reset;
        } else {
            out += C.cyan + "│" + C.reset + padded + C.cyan + "│\n" + C.reset;
        }
    }
    
    // Draw box bottom
    out += C.cyan + "└" + "─".repeat(this.width) + "┘\n" + C.reset;
    process.stdout.write(out);
};

Menu.prototype.run = function() {
    var self = this;
    return new Promise(function(resolve) {
        process.stdin.setRawMode(true);
        self.render();
        
        function onData(buf) {
            var str = buf.toString();
            if (str === '\u0003') { // Ctrl+C
                process.stdout.write(C.show + C.clear);
                process.exit(0);
            } else if (str === '\x1b[A') { // Up
                self.selected = (self.selected > 0) ? self.selected - 1 : self.choices.length - 1;
                self.render();
            } else if (str === '\x1b[B') { // Down
                self.selected = (self.selected < self.choices.length - 1) ? self.selected + 1 : 0;
                self.render();
            } else if (str === '\r' || str === '\n') { // Enter
                process.stdin.removeListener('data', onData);
                process.stdin.setRawMode(false);
                process.stdout.write(C.clear);
                resolve(self.choices[self.selected].value);
            }
        }
        process.stdin.on('data', onData);
    });
};

function readLineInput(promptStr) {
    return new Promise(function(resolve) {
        process.stdout.write(C.clear + promptStr + " ");
        process.stdin.setRawMode(false);
        var readline = require('readline');
        var rl = readline.createInterface({ input: process.stdin, output: process.stdout });
        rl.on('line', function(line) {
            rl.close();
            resolve(line.trim());
        });
    });
}

function getAllCategories() {
    var cats = {};
    [PROPOSED_DIR, APPROVED_DIR, COMPLETED_DIR].forEach(function(d) {
        if (fs.existsSync(d)) {
            var files = fs.readdirSync(d);
            files.forEach(function(f) {
                var stat = fs.statSync(path.join(d, f));
                if (stat.isDirectory()) cats[f] = true;
            });
        }
    });
    return Object.keys(cats).sort();
}

async function proposeFeature() {
    var title = await readLineInput(C.cyan + "Enter feature title:" + C.reset);
    if (!title) return;
    
    var cats = getAllCategories();
    var choices = [{ name: "+ Create New Category", value: "new" }];
    cats.forEach(function(c) { choices.push({ name: c, value: c }); });
    choices.push({ name: "← Cancel", value: "cancel" });
    
    var menu = new Menu("Select a Category", choices, 50);
    var category = await menu.run();
    if (category === "cancel") return;
    
    if (category === "new") {
        category = await readLineInput(C.cyan + "Enter new category name:" + C.reset);
        category = category.replace(/[^a-zA-Z0-9_-]/g, '_');
        if (!category) return;
    }
    
    var safeTitle = title.replace(/[^a-zA-Z0-9_-]/g, '_');
    var filename = safeTitle + ".md";
    var destDir = path.join(PROPOSED_DIR, category);
    ensureDir(destDir);
    
    var fullPath = path.join(destDir, filename);
    var tpl = "# " + title + "\n\n**Category**: " + category + "\n**Status**: Proposed\n\n## Description\n\n[Provide a detailed description of the feature here]\n";
    fs.writeFileSync(fullPath, tpl);
    
    process.stdout.write(C.green + "\n✔ Created new feature proposal: " + fullPath + "\n\n" + C.reset);
    await readLineInput("Press Enter to continue...");
}

async function readFeatureFile(filePath) {
    var content = fs.readFileSync(filePath).toString();
    var lines = content.split('\n');
    var scrollY = 0;
    var termHeight = 24; // fallback, in node process.stdout.rows
    
    return new Promise(function(resolve) {
        process.stdin.setRawMode(true);
        function draw() {
            var out = C.clear + C.bold + C.bgCyan + "=== Reading: " + path.basename(filePath) + " === (Up/Down to scroll, 'q' to exit)" + C.reset + "\n\n";
            for (var i = 0; i < termHeight - 4; i++) {
                if (scrollY + i < lines.length) {
                    out += lines[scrollY + i] + "\n";
                }
            }
            process.stdout.write(out);
        }
        
        draw();
        function onData(buf) {
            var str = buf.toString();
            if (str === 'q' || str === '\u0003' || str === '\r' || str === '\n') {
                process.stdin.removeListener('data', onData);
                resolve();
            } else if (str === '\x1b[A') { // Up
                if (scrollY > 0) { scrollY--; draw(); }
            } else if (str === '\x1b[B') { // Down
                if (scrollY < lines.length - (termHeight - 4)) { scrollY++; draw(); }
            }
        }
        process.stdin.on('data', onData);
    });
}

async function moveFeatureFlow(sourceDir, destDir, actionName) {
    if (!fs.existsSync(sourceDir)) {
        console.log(C.yellow + "No features available.\n" + C.reset);
        await readLineInput("Press Enter to return...");
        return;
    }
    
    var cats = fs.readdirSync(sourceDir).filter(function(f) {
        return fs.statSync(path.join(sourceDir, f)).isDirectory();
    });
    
    var validCats = [];
    cats.forEach(function(c) {
        var files = fs.readdirSync(path.join(sourceDir, c)).filter(function(f) { return f.endsWith('.md'); });
        if (files.length > 0) validCats.push({ name: c, files: files });
    });
    
    if (validCats.length === 0) {
        console.log(C.yellow + "No features available to " + actionName.toLowerCase() + ".\n" + C.reset);
        await readLineInput("Press Enter to return...");
        return;
    }
    
    var selectedCat = validCats[0];
    if (validCats.length > 1) {
        var catChoices = validCats.map(function(c) { return { name: c.name + " (" + c.files.length + " items)", value: c }; });
        catChoices.push({ name: "← Cancel", value: "cancel" });
        var menu = new Menu("Select a Category", catChoices, 50);
        selectedCat = await menu.run();
        if (selectedCat === "cancel") return;
    }
    
    while (true) {
        if (selectedCat.files.length === 0) break;
        var fChoices = selectedCat.files.map(function(f) { return { name: f.replace('.md', ''), value: f }; });
        fChoices.push({ name: "← Go Back", value: "cancel" });
        var fMenu = new Menu("Select a Feature to " + actionName, fChoices, 70);
        var selectedFile = await fMenu.run();
        if (selectedFile === "cancel") return;
        
        var sourcePath = path.join(sourceDir, selectedCat.name, selectedFile);
        
        var actionChoices = [
            { name: "📖 Read Feature", value: "read" },
            { name: "👍 " + actionName + " Feature", value: "move" },
            { name: "← Go Back", value: "back" }
        ];
        var aMenu = new Menu("Action for " + selectedFile, actionChoices, 50);
        var action = await aMenu.run();
        
        if (action === "read") {
            await readFeatureFile(sourcePath);
        } else if (action === "move") {
            var targetCatDir = path.join(destDir, selectedCat.name);
            ensureDir(targetCatDir);
            var targetPath = path.join(targetCatDir, selectedFile);
            fs.renameSync(sourcePath, targetPath);
            process.stdout.write(C.green + "\n✔ Feature moved to " + actionName.toLowerCase() + ": " + targetPath + "\n\n" + C.reset);
            await readLineInput("Press Enter to continue...");
            selectedCat.files = selectedCat.files.filter(function(f) { return f !== selectedFile; });
        }
    }
}

async function main() {
    process.stdout.write(C.hide);
    ensureDir(PROPOSED_DIR);
    ensureDir(APPROVED_DIR);
    ensureDir(COMPLETED_DIR);
    
    while (true) {
        var choices = [
            { name: "📝 Propose New Feature", value: "propose" },
            { name: "👍 Approve Feature", value: "approve" },
            { name: "✅ Complete Feature", value: "complete" },
            { name: "🚪 Exit", value: "exit" }
        ];
        
        var menu = new Menu("Curica Feature Manager", choices, 50);
        var action = await menu.run();
        
        if (action === "propose") await proposeFeature();
        else if (action === "approve") await moveFeatureFlow(PROPOSED_DIR, APPROVED_DIR, "Approve");
        else if (action === "complete") await moveFeatureFlow(APPROVED_DIR, COMPLETED_DIR, "Complete");
        else if (action === "exit") break;
    }
    
    process.stdout.write(C.show + C.clear + "Goodbye!\n");
    process.exit(0);
}

main().catch(function(e) {
    process.stdout.write(C.show);
    console.error(e);
});
